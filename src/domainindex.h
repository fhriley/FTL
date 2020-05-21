#ifndef DOMAIN_INDEX_H
#define DOMAIN_INDEX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "FTL.h"

void FTL_DomainIndexInit(void);
int findDomainID(const char *domainString, const bool count);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // DOMAIN_INDEX_H
