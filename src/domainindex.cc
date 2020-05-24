#include <map>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "domainindex.h"

extern "C" {
#include "FTL.h"
#include "shmem.h"
#include "memory.h"
#include "datastructure.h"
#include "regex_r.h"
#include "thread.h"
// global variable killed
#include "signals.h"
#include "log.h"
}

#define SERVER_SOCKET "#index_sock_s"
#define CLIENT_SOCKET "#index_sock_c"

struct cache_key_t
{
    int domainID;
    int clientID;

    bool operator< (const cache_key_t& key) const
    {
        return (domainID < key.domainID ||
                (domainID == key.domainID && clientID < key.clientID));
    }
};

enum command_e
{
    COMMAND_DOMAIN,
    COMMAND_CACHE,
};

struct cmd_t
{
    command_e command;
    union
    {
        struct
        {
            bool count;
            char name[256];
        } domain;
        struct
        {
            int domainId;
            int clientId;
        } cache;
    } u;
};

static void* indexThread_(void*);
static int findDomainID_(const char *domainString, const bool count);
int findDomainID(const char *domainString, const bool count);
static int findCacheID_(int domainID, int clientID);

static std::map<std::string, int> domainsDataByString_;
static std::map<cache_key_t, int> cacheByDomainAndClient_;
static pthread_t index_;
static int socket_;
static pid_t mainProcess_;
struct sockaddr_un server_;

/*
 * Copy string src to buffer dst of size dsize.  At most dsize-1
 * chars will be copied.  Always NUL terminates (unless dsize == 0).
 * Returns strlen(src); if retval >= dsize, truncation occurred.
 */
static size_t strlcpy(char * __restrict dst, const char * __restrict src, size_t dsize)
{
	const char *osrc = src;
	size_t nleft = dsize;

	/* Copy as many bytes as will fit. */
	if (nleft != 0) {
		while (--nleft != 0) {
			if ((*dst++ = *src++) == '\0')
				break;
		}
	}

	/* Not enough room in dst, add NUL and traverse rest of src. */
	if (nleft == 0) {
		if (dsize != 0)
			*dst = '\0';		/* NUL-terminate dst */
		while (*src++)
			;
	}

	return(src - osrc - 1);	/* count does not include NUL */
}

int createSocket_(const char* path, struct sockaddr_un* addr)
{
    int sock = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (sock < 0)
    {
        char buf[256];
        logg("Failed to create index socket: %s", strerror_r(errno, buf, sizeof(buf)));
        exit(EXIT_FAILURE);
    }

    int enable = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0)
    {
        char buf[256];
        logg("setsockopt failed: %s", strerror_r(errno, buf, sizeof(buf)));
        exit(EXIT_FAILURE);
    }
   
    addr->sun_family = AF_UNIX;
    strlcpy(addr->sun_path, path, sizeof(addr->sun_path));
    addr->sun_path[0] = 0;
   
    logg("binding to %s", &addr->sun_path[1]);
    if (bind(sock, (struct sockaddr *)addr, sizeof(struct sockaddr_un)) < 0)
    {
        logg("closing sock");
        close(sock);
        char buf[256];
        logg("Failed to bind index socket: %s", strerror_r(errno, buf, sizeof(buf)));
        exit(EXIT_FAILURE);
    }

    return sock;
}

static void send_(int sock, const void* data, int size, const struct sockaddr_un* addr, socklen_t addrLen)
{
    logg("sending to %s", &addr->sun_path[1]);
    if (sendto(sock, data, size, 0, (struct sockaddr *)addr, addrLen) < 0)
    {
        char buf[256];
        logg("FATAL: sendto() failed: %s", strerror_r(errno, buf, sizeof(buf)));
		exit(EXIT_FAILURE);
    }
}

static int recv_(int sock, void* data, int size, struct sockaddr_un* addr, socklen_t* addrLen)
{
    while (true)
    {
        ssize_t received = recvfrom(sock, data, size, 0, (struct sockaddr*)addr, addrLen);
        if (received < 0)
        {
            if (errno == EINTR || errno == EWOULDBLOCK)
            {
                continue;
            }
            char buf[256];
            logg("FATAL: index fork recv() failed: %s", strerror_r(errno, buf, sizeof(buf)));
            exit(EXIT_FAILURE);
        }
        return received;
    }
}

void FTL_DomainIndexInit(void)
{
    mainProcess_ = getpid();
    socket_ = createSocket_(SERVER_SOCKET, &server_);
    //index_ = create_thread_or_fail(indexThread_,  "index");
}

static void* indexThread_(void*)
{
    cmd_t cmd;
    while (!killed)
    {
        struct sockaddr_un from;
        socklen_t addrlen = sizeof(struct sockaddr_un);
        recv_(socket_, &cmd, sizeof(cmd), &from, &addrlen);
        
        int id = -1;
        switch (cmd.command)
        {
            case COMMAND_DOMAIN:
                logg("got domain: %s, count: %d", cmd.u.domain.name, cmd.u.domain.count);
                id = findDomainID_(cmd.u.domain.name, cmd.u.domain.count);
                logg("sending domain id %d", id);
                break;
            case COMMAND_CACHE:
                logg("got domainId: %d, clientId: %d", cmd.u.cache.domainId, cmd.u.cache.clientId);
                id = findCacheID_(cmd.u.cache.domainId, cmd.u.cache.clientId);
                logg("sending cache id %d", id);
                break;
            default:
                logg("FATAL: index got invalid command: %d", cmd.command);
                exit(EXIT_FAILURE);
        }

        // Send id back
        send_(socket_, &id, sizeof(id), &from, addrlen);

        logg("sent id %d", id);
    }

    logg("closing server sock");
    close(socket_);

    return NULL;
}

static bool isForked_()
{
    return mainProcess_ != getpid();
}

static int getIdFromMainProcess_(cmd_t* cmd, int sendSize)
{
    logg("sendSize: %d", sendSize);

    struct sockaddr_un client;
    int sock = createSocket_(CLIENT_SOCKET, &client);

    send_(sock, cmd, sendSize, &server_, sizeof(sa_family_t) + sizeof(SERVER_SOCKET) + 2);

    int id = -1;
    recv_(sock, &id, sizeof(id), NULL, NULL);

    logg("closing client sock");
    close(sock);
    logg("received id %d", id);
    return id;
}

static void incrementCount_(int domainId, bool count)
{
    if (count)
    {
        domainsData* domain = getDomain(domainId, false);
        if (domain == NULL)
        {
            logg("FATAL: Encountered serious memory error in findDomainID()");
            exit(EXIT_FAILURE);
        }
		
        domain->count++;
    }
}

static int findDomainID_(const char *domainString, const bool count)
{
    int domainID = counters->domains;
    auto result = domainsDataByString_.insert(std::map<std::string, int>::value_type(domainString, domainID));
    if (result.second)
    {
        // Didn't have it indexed, create a new entry
        memory_check(DOMAINS);

        domainsData* domain = getDomain(domainID, false);
        if(domain == NULL)
        {
            //logg("ERROR: Encountered serious memory error in findDomainID()");
            domainsDataByString_.erase(result.first);
            unlock_shm();
            return -1;
        }

        // Set magic byte
        domain->magic = MAGICBYTE;
        // Set its counter to 1 only if this domain is to be counted
        // Domains only encountered during CNAME inspection are NOT counted here
        domain->count = count ? 1 : 0;
        // Set blocked counter to zero
        domain->blockedcount = 0;
        // Store domain name - no need to check for NULL here as it doesn't harm
        domain->domainpos = addstr(domainString);
        // Increase counter by one
        counters->domains++;
    }
    else
    {
        // Already have it indexed
        domainID = result.first->second;
        incrementCount_(domainID,  count);
    }

    return domainID;
}

// A forked process has a copy of the main process's index. However, by the time the
// forked process gets to this call, the local index could be out of sync. We rectify
// this with the following procedure:
// 1) Check if the domain is in our local index. If so, there is nothing more to do.
// 2) 
int findDomainID(const char *domainString, const bool count)
{
    int domainID = -1;

    if (isForked_())
    {
        // Check if we already have it locally
        auto result = domainsDataByString_.find(domainString);

        if (result == domainsDataByString_.end())
        {
            // We don't have it, get it from the main process
            cmd_t cmd;
            cmd.command = COMMAND_DOMAIN;
            int len = strlcpy(cmd.u.domain.name, domainString, sizeof(cmd.u.domain.name));
            cmd.u.domain.count = count;
            domainID = getIdFromMainProcess_(&cmd,
                sizeof(cmd.command) + sizeof(cmd.u.domain) - sizeof(cmd.u.domain.name) + len + 1);

            // Insert it into local index
            domainsDataByString_.insert(std::map<std::string, int>::value_type(domainString, domainID));
        }
        else
        {
            // We do have it, return it
            domainID = result->second;
            incrementCount_(domainID,  count);
        }
    }
    else
    {
        // Call the normal function
        domainID = findDomainID_(domainString, count);
    }

    return domainID;
}

static int findCacheID_(int domainID, int clientID)
{
    // Get ID of new cache entry
    int cacheID = counters->dns_cache_size;

    cache_key_t key = {domainID, clientID};
    auto result = cacheByDomainAndClient_.insert(std::map<cache_key_t, int>::value_type(key, cacheID));
    if (result.second)
    {
        memory_check(DNS_CACHE);

        DNSCacheData* dns_cache = getDNSCache(cacheID, false);
        if (dns_cache == NULL)
        {
            //logg("ERROR: Encountered serious memory error in findCacheID()");
            cacheByDomainAndClient_.erase(result.first);
            return -1;
        }

        // Initialize cache entry
        dns_cache->magic = MAGICBYTE;
        dns_cache->blocking_status = UNKNOWN_BLOCKED;
        dns_cache->domainID = domainID;
        dns_cache->clientID = clientID;
        dns_cache->force_reply = 0u;

        // Increase counter by one
        counters->dns_cache_size++;
    }
    else
    {
        cacheID = result.first->second;
    }

    return cacheID;
}

int findCacheID(int domainID, int clientID)
{
    int cacheID = -1;

    if (isForked_())
    {
        // Check if we already have it locally
        cache_key_t key = {domainID, clientID};
        auto result = cacheByDomainAndClient_.find(key);

        if (result == cacheByDomainAndClient_.end())
        {
            // We don't have it, get it from the main process
            cmd_t cmd;
            cmd.command = COMMAND_CACHE;
            cmd.u.cache.domainId = domainID;
            cmd.u.cache.clientId = clientID;
            cacheID = getIdFromMainProcess_(&cmd, sizeof(cmd.command) + sizeof(cmd.u.cache));

            // Insert it into our local index
            cacheByDomainAndClient_.insert(std::map<cache_key_t, int>::value_type(key, cacheID));
        }
        else
        {
            // We do have it, return it
            cacheID = result->second;
        }
    }
    else
    {
        // Call the normal function
        cacheID = findCacheID_(domainID, clientID);
    }

    return cacheID;
}
