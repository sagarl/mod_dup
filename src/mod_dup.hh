/*
* mod_dup - duplicates apache requests
* 
* Copyright (C) 2013 Orange
* 
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#pragma once

#include <httpd.h>
#include <http_config.h>
#include <http_request.h>
#include <http_protocol.h>
#include <apr_pools.h>
#include <apr_hooks.h>
#include <queue>
#include <unistd.h>
#include <curl/curl.h>
#include <iostream>
#include <boost/lexical_cast.hpp>

#include "Log.hh"
#include "RequestProcessor.hh"
#include "ThreadPool.hh"

namespace DupModule {


/**
 * A structure that holds the configuration specific to the location
 */
struct DupConf {
    char        *dirName;
    int         payload;
};

/**
 * @brief Initialize our the processor and thread pool pre-config
 * @param pPool the apache pool
 * @return Always OK
 */
int
preConfig(apr_pool_t * pPool, apr_pool_t * pLog, apr_pool_t * pTemp);

/**
 * @brief allocate a pointer to a string which will hold the path for the dir config if mod_dup is active on it
 * @param pPool the apache pool on which to allocate data
 * @param pDirName the directory name for which to create data
 * @return a void pointer to newly allocated object
 */
void *
createDirConfig(apr_pool_t *pPool, char *pDirName);

/**
 * @brief Initialize logging post-config
 * @param pPool the apache pool
 * @param pServer the corresponding server record
 * @return Always OK
 */
int
postConfig(apr_pool_t * pPool, apr_pool_t * pLog, apr_pool_t * pTemp, server_rec * pServer);

/**
 * @brief Set the destination host and port
 * @param pParams miscellaneous data
 * @param pCfg user data for the directory/location
 * @param pDestionation the destination in <host>[:<port>] format
 * @return Always DECLINED to let other modules handle the request
 */
const char*
setDestination(cmd_parms* pParams, void* pCfg, const char* pDestination);

/**
 * @brief Set the minimum and maximum number of threads
 * @param pParams miscellaneous data
 * @param pCfg user data for the directory/location
 * @param pMin the minimum number of threads
 * @param pMax the maximum number of threads
 * @return NULL if parameters are valid, otherwise a string describing the error
 */
const char*
setThreads(cmd_parms* pParams, void* pCfg, const char* pMin, const char* pMax);

/**
 * @brief Set the timeout for outgoing requests
 * @param pParams miscellaneous data
 * @param pCfg user data for the directory/location
 * @param pTimeout the timeout for outgoing requests in ms
 * @return NULL if parameters are valid, otherwise a string describing the error
 */
const char*
setTimeout(cmd_parms* pParams, void* pCfg, const char* pTimeout);

/**
 * @brief Set the minimum and maximum queue size
 * @param pParams miscellaneous data
 * @param pCfg user data for the directory/location
 * @param pMin the minimum queue size
 * @param pMax the maximum queue size
 * @return NULL if parameters are valid, otherwise a string describing the error
 */
const char*
setQueue(cmd_parms* pParams, void* pCfg, const char* pMin, const char* pMax);

/**
 * @brief Add a substitution definition
 * @param pParams miscellaneous data
 * @param pCfg user data for the directory/location
 * @param pField the field on which to do the substitution
 * @param pMatch the regexp matching what should be replaced
 * @param pReplace the value which the match should be replaced with
 * @return NULL if parameters are valid, otherwise a string describing the error
 */
const char*
setSubstitution(cmd_parms* pParams, void* pCfg, tFilterBase::eFilterScope pScope, const char *pField, const char* pMatch, const char* pReplace);

const char*
setHeaderSubstitution(cmd_parms* pParams, void* pCfg, const char *pField, const char* pMatch, const char* pReplace);

/**
 * @brief Activate duplication
 * @param pParams miscellaneous data
 * @param pCfg user data for the directory/location
 * @return NULL
 */
const char*
setActive(cmd_parms* pParams, void* pCfg);

/**
 * @brief Add a filter definition
 * @param pParams miscellaneous data
 * @param pCfg user data for the directory/location
 * @param pField the field on which to do the substitution
 * @param pFilter a reg exp which has to match for this request to be duplicated
 * @return NULL if parameters are valid, otherwise a string describing the error
 */
const char*
setFilter(cmd_parms* pParams, void* pCfg, const char *pType, const char *pField, const char* pFilter);

/**
 * @brief Clean up before the child exits
 */
apr_status_t
cleanUp(void *);

/**
 * @brief init curl and our own thread pool on child init
 * @param pPool the apache pool
 * @param pServer the apache server record
 */
void
childInit(apr_pool_t *pPool, server_rec *pServer);

/**
 * @brief register hooks in apache
 * @param pPool the apache pool
 */
void registerHooks(apr_pool_t *pPool);

}
