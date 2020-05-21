#include <map>
#include <thread>
#include "domainindex.h"

extern "C" {
#include "shmem.h"
#include "memory.h"
#include "datastructure.h"
#include "regex_r.h"
}

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

static std::map<std::string, int> domainsDataByString_;
static std::map<cache_key_t, int> cacheByDomainAndClient_;

void FTL_DomainIndexInit(void)
{

}

int findDomainID(const char *domainString, const bool count)
{
    // TODO: check if we are forked, and if so, call to daemon to do the get

    domainsData* domain = NULL;
    int domainID = counters->domains;

    auto result = domainsDataByString_.insert(std::map<std::string, int>::value_type(domainString, domainID));
    if (result.second)
    {
        memory_check(DOMAINS);

        domain = getDomain(domainID, false);
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
        domainID = result.first->second;

        domain = getDomain(domainID, false);
        if (domain == NULL)
        {
            //logg("ERROR: Encountered serious memory error in findDomainID()");
            unlock_shm();
            return -1;
        }

        if (count)
        {
		    domain->count++;
        }
    } 

    return domainID;
}

int findCacheID(int domainID, int clientID)
{
    // TODO: check if we are forked, and if so, call to daemon to do the get

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
