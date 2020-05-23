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
            char name[256];
            bool count;
        } domain;
        struct
        {
            int domainId;
            int clientId;
        } cache;
    } u;
};

static void* indexThread_(void*);
int findDomainID(const char *domainString, const bool count);

static std::map<std::string, int> domainsDataByString_;
static std::map<cache_key_t, int> cacheByDomainAndClient_;
static cmd_t cmd_;
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

void FTL_DomainIndexInit(void)
{
    mainProcess_ = getpid();

    socket_ = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (socket_ < 0)
    {
        char buf[256];
        logg("Failed to create index socket: %s", strerror_r(errno, buf, sizeof(buf)));
        exit(EXIT_FAILURE);
    }
   
    memset(&server_, 0, sizeof(struct sockaddr_un));
    server_.sun_family = AF_UNIX;
    strlcpy(server_.sun_path, SERVER_SOCKET, sizeof(server_.sun_path));
    server_.sun_path[0] = 0;
   
    if (bind(socket_, (const struct sockaddr *)&server_, sizeof(struct sockaddr_un)) < 0)
    {
        close(socket_);
        char buf[256];
        logg("Failed to bind index socket: %s", strerror_r(errno, buf, sizeof(buf)));
        exit(EXIT_FAILURE);
    }
    index_ = create_thread_or_fail(indexThread_,  "index");
}

static void* indexThread_(void*)
{
    while (!killed)
    {
        struct sockaddr_un from;
        socklen_t addrlen = sizeof(struct sockaddr_un);
        ssize_t received = recvfrom(socket_, &cmd_, sizeof(cmd_), 0, (sockaddr *)&from, &addrlen);
        if (received < 0)
        {
            if (errno == EINTR || errno == EWOULDBLOCK)
            {
                continue;
            }
            char buf[256];
            logg("FATAL: index recvfrom() failed: %s", strerror_r(errno, buf, sizeof(buf)));
            exit(EXIT_FAILURE);
        }
        
        int id = -1;
        switch (cmd_.command)
        {
            case COMMAND_DOMAIN:
                id = findDomainID(cmd_.u.domain.name, cmd_.u.domain.count);
                logg("sending domain id %d", id);
                break;
            case COMMAND_CACHE:
                id = findCacheID(cmd_.u.cache.domainId, cmd_.u.cache.clientId);
                logg("sending cache id %d", id);
                break;
            default:
                logg("FATAL: index got invalid command: %d", cmd_.command);
                exit(EXIT_FAILURE);
        }

        // Send id back
        if (sendto(socket_, &id, sizeof(id), 0, (struct sockaddr *)&from, addrlen) < 0)
        {
            char buf[256];
            logg("FATAL: index sendto() failed: %s", strerror_r(errno, buf, sizeof(buf)));
            exit(EXIT_FAILURE);
        }

        logg("sent id %d", id);
    }

    close(socket_);

    return NULL;
}

static bool isForked()
{
    return mainProcess_ != getpid();
}

static int getIdFromMainProcess_(int sendSize)
{
    int sock = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        char buf[256];
        logg("Failed to create fork index socket: %s", strerror_r(errno, buf, sizeof(buf)));
        exit(EXIT_FAILURE);
    }

    struct sockaddr_un client;
    memset(&client, 0, sizeof(struct sockaddr_un));
    client.sun_family = AF_UNIX;
    strlcpy(client.sun_path, CLIENT_SOCKET, sizeof(client.sun_path));
    client.sun_path[0] = 0;
   
    if (bind(sock, (struct sockaddr *)&client, sizeof(struct sockaddr_un)) < 0)
    {
        close(sock);
        char buf[256];
        logg("Failed to bind fork index socket: %s", strerror_r(errno, buf, sizeof(buf)));
        exit(EXIT_FAILURE);
    }

    if (sendto(sock, &cmd_, sendSize, 0, (struct sockaddr *)&server_, sizeof(struct sockaddr_un)) < 0)
    {
        close(sock);
        char buf[256];
        logg("FATAL: index fork sendto() failed: %s", strerror_r(errno, buf, sizeof(buf)));
		exit(EXIT_FAILURE);
    }

    int id = -1;
    while (true)
    {
        ssize_t received = recv(sock, &id, sizeof(id), 0);
        if (received < 0)
        {
            if (errno == EINTR || errno == EWOULDBLOCK)
            {
                continue;
            }
            close(sock);
            char buf[256];
            logg("FATAL: index fork recv() failed: %s", strerror_r(errno, buf, sizeof(buf)));
            exit(EXIT_FAILURE);
        }
        close(sock);
        break;
    }

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

// A forked process has a copy of the main process's index. However, by the time the
// forked process gets to this call, the local index could be out of sync. We rectify
// this with the following procedure:
// 1) Check if the domain is in our local index. If so, there is nothing more to do.
// 2) 
int findDomainID(const char *domainString, const bool count)
{
    int domainID = -1;

    if (isForked())
    {
        auto result = domainsDataByString_.find(domainString);
        if (result == domainsDataByString_.end())
        {
            // Didn't have it in local index, get it from the main process
            cmd_.command = COMMAND_DOMAIN;
            int len = strlcpy(cmd_.u.domain.name, domainString, sizeof(cmd_.u.domain.name));
            cmd_.u.domain.count = count;
            domainID = getIdFromMainProcess_(sizeof(cmd_.u.domain.count) + len);
            // Insert it into local index
            domainsDataByString_.insert(std::map<std::string, int>::value_type(domainString, domainID));
        }
        else
        {
            // Already have it in local index
            domainID = result->second;
            incrementCount_(domainID,  count);
        }
    }
    else
    {
        domainID = counters->domains;

        auto result = domainsDataByString_.insert(std::map<std::string, int>::value_type(domainString, domainID));
        if (result.second)
        {
            // Didn't have it indexed, create a new entry
            memory_check(DOMAINS);

            domainsData* domain = getDomain(domainID, false);
            if(domain == NULL)
            {
                //logg("ERROR: Encountered serious memory error in findDomainID()");
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
    }

    return domainID;
}

int findCacheID(int domainID, int clientID)
{
    int cacheID = -1;

    if (isForked())
    {
        cache_key_t key = {domainID, clientID};
        auto result = cacheByDomainAndClient_.find(key);
        if (result == cacheByDomainAndClient_.end())
        {
            // Didn't have it in local index, get it from the main process
            cmd_.command = COMMAND_CACHE;
            cmd_.u.cache.domainId = domainID;
            cmd_.u.cache.clientId = clientID;
            cacheID = getIdFromMainProcess_(sizeof(cmd_.u.cache));
            // Insert it into local index
            cacheByDomainAndClient_.insert(std::map<cache_key_t, int>::value_type(key, cacheID));
        }
        else
        {
            // Already have it in local index
            cacheID = result->second;
        }
    }
    else
    {
        // Get ID of new cache entry
        cacheID = counters->dns_cache_size;

        cache_key_t key = {domainID, clientID};
        auto result = cacheByDomainAndClient_.insert(std::map<cache_key_t, int>::value_type(key, cacheID));
        if (result.second)
        {
            memory_check(DNS_CACHE);

            DNSCacheData* dns_cache = getDNSCache(cacheID, false);
            if (dns_cache == NULL)
            {
                //logg("ERROR: Encountered serious memory error in findCacheID()");
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
    }

    return cacheID;
}
