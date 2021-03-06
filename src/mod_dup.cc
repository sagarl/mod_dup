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

#include <httpd.h>
#include <http_config.h>
#include <http_request.h>
#include <http_protocol.h>
#include <http_connection.h>
#include <apr_pools.h>
#include <apr_hooks.h>
#include "apr_strings.h"
#include <unistd.h>
#include <curl/curl.h>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
#include <set>

#include "mod_dup.hh"

extern module AP_DECLARE_DATA dup_module;

namespace alg = boost::algorithm;

namespace DupModule {

const char *gName = "Dup";

#define INVALID_SCOPE_VALUE "Invalid Filter value (ALL, BODY, HEADER)."

RequestProcessor *gProcessor;
ThreadPool<RequestInfo> *gThreadPool;

struct BodyHandler {
    BodyHandler() : body(), sent(0) {}
    std::string body;
    int sent;
};

#define GET_CONF_FROM_REQUEST(request) reinterpret_cast<DupConf **>(ap_get_module_config(request->per_dir_config, &dup_module))
apr_status_t
analyseRequest(ap_filter_t *pF, apr_bucket_brigade *pB ) {
    request_rec *pRequest = pF->r;
    if (pRequest) {
	struct DupConf **tConf = GET_CONF_FROM_REQUEST(pRequest);
	if (!tConf || !*tConf) {
            return OK;
	}
        // Do we have a context?
        if (!pF->ctx) {
            pF->ctx = new BodyHandler();
        } else if (pF->ctx == (void *)1) {
            return OK;
        }
        BodyHandler *pBH = static_cast<BodyHandler *>(pF->ctx);
        // Body is stored only if the payload flag is activated
        for (apr_bucket *b = APR_BRIGADE_FIRST(pB);
             b != APR_BRIGADE_SENTINEL(pB);
             b = APR_BUCKET_NEXT(b) ) {
#ifndef UNIT_TESTING
            // Metadata end of stream
            if ( APR_BUCKET_IS_EOS(b) ) {
#endif
                pBH->sent = 1;

                Log::debug("Pushing a request, body size:%s", boost::lexical_cast<std::string>(pBH->body.size()).c_str());
                Log::debug("Uri:%s, dir name:%s", pRequest->uri, (*tConf)->dirName);
                gThreadPool->push(RequestInfo((*tConf)->dirName, pRequest->uri, pRequest->args ? pRequest->args : "", &pBH->body));
                delete pBH;
                pF->ctx = (void *)1;
                break;
#ifndef UNIT_TESTING
            }
#endif
            const char* lReqPart = NULL;
            apr_size_t lLength = 0;
            apr_status_t lStatus = apr_bucket_read(b, &lReqPart, &lLength, APR_BLOCK_READ);
            if ((lStatus != APR_SUCCESS) || (lReqPart == NULL)) {
                continue;
            }
            pBH->body += std::string(lReqPart, lLength);
        }
    }
    return OK;
}

/// @brief the output filter callback
///        Whenever a pns response is send to client, the filter looks at the
///        state of the context to transform the ISE to the good one
/// @param pFilter
/// @param pBrigade
/// @param pMode
/// @param pBlock
/// @param pReadbytes
static apr_status_t
filterHandler(ap_filter_t *pFilter, apr_bucket_brigade *pBrigade, ap_input_mode_t pMode, apr_read_type_e pBlock, apr_off_t pReadbytes)
{
    apr_status_t lStatus = ap_get_brigade(pFilter->next, pBrigade, pMode, pBlock, pReadbytes);
    if (lStatus != APR_SUCCESS) {
        return lStatus;
    }
    return analyseRequest(pFilter, pBrigade);
}

/**
 * @brief allocate a pointer to a string which will hold the path for the dir config if mod_dup is active on it
 * @param pPool the apache pool on which to allocate data
 * @param pDirName the directory name for which to create data
 * @return a void pointer to newly allocated object
 */
void *
createDirConfig(apr_pool_t *pPool, char *pDirName)
{
    struct DupConf **lConf = (struct DupConf **) apr_pcalloc(pPool, sizeof(*lConf));
    return reinterpret_cast<void *>(lConf);
}

/**
 * @brief Initialize the processor and thread pool pre-config
 * @param pPool the apache pool
 * @return Always OK
 */
int
preConfig(apr_pool_t * pPool, apr_pool_t * pLog, apr_pool_t * pTemp) {
    gProcessor = new RequestProcessor();
    gThreadPool = new ThreadPool<RequestInfo>(boost::bind(&RequestProcessor::run, gProcessor, _1), POISON_REQUEST);
    // Add the request timeout stat provider. Compose the lexical_cast with getTimeoutCount so that the resulting stat provider returns a string
    gThreadPool->addStat("#TmOut", boost::bind(boost::lexical_cast<std::string, unsigned int>,
                                               boost::bind(&RequestProcessor::getTimeoutCount, gProcessor)));
    gThreadPool->addStat("#DupReq", boost::bind(boost::lexical_cast<std::string, unsigned int>,
                                                boost::bind(&RequestProcessor::getDuplicatedCount, gProcessor)));
    return OK;
}

/**
 * @brief Initialize logging post-config
 * @param pPool the apache pool
 * @param pServer the corresponding server record
 * @return Always OK
 */
int
postConfig(apr_pool_t * pPool, apr_pool_t * pLog, apr_pool_t * pTemp, server_rec * pServer) {
	Log::init();

    ap_add_version_component(pPool, "Dup/1.0") ;
    return OK;
}

/**
 * @brief Set the destination host and port
 * @param pParams miscellaneous data
 * @param pCfg user data for the directory/location
 * @param pDestionation the destination in <host>[:<port>] format
 * @return NULL if parameters are valid, otherwise a string describing the error
 */
const char*
setDestination(cmd_parms* pParams, void* pCfg, const char* pDestination) {
	if (!pDestination || strlen(pDestination) == 0) {
		return "Missing destination";
	}
	gProcessor->setDestination(pDestination);
	return NULL;
}

/**
 * @brief Set the program name used in the stats messages
 * @param pParams miscellaneous data
 * @param pCfg user data for the directory/location
 * @param pName the name to be used
 * @return NULL if parameters are valid, otherwise a string describing the error
 */
const char*
setName(cmd_parms* pParams, void* pCfg, const char* pName) {
	if (!pName || strlen(pName) == 0) {
		return "Missing program name";
	}
	gThreadPool->setProgramName(pName);
	return NULL;
}

/**
 * @brief Set the url enc/decoding style
 * @param pParams miscellaneous data
 * @param pCfg user data for the directory/location
 * @param pUrlCodec the url enc/decoding style to use
 * @return NULL if parameters are valid, otherwise a string describing the error
 */
const char*
setUrlCodec(cmd_parms* pParams, void* pCfg, const char* pUrlCodec) {
	if (!pUrlCodec || strlen(pUrlCodec) == 0) {
		return "Missing url codec style";
	}
	gProcessor->setUrlCodec(pUrlCodec);
	return NULL;
}

const char*
setPayload(cmd_parms* pParams, void* pCfg, const char* pDestination) {
	const char *lErrorMsg = setActive(pParams, pCfg);
	if (lErrorMsg) {
            return lErrorMsg;
	}
    struct DupConf *tC = *reinterpret_cast<DupConf **>(pCfg);
    tC->payload = 1;
    return NULL;
}

const char*
setRawSubstitute(cmd_parms* pParams, void* pCfg,
                 const char* pType,
                 const char* pMatch, const char* pReplace){
    const char *lErrorMsg = setActive(pParams, pCfg);
    if (lErrorMsg) {
        return lErrorMsg;
    }
    try {
        gProcessor->addRawSubstitution(pParams->path, pMatch, pReplace,
                                       tFilterBase::GetScopeFromString(pType));
    } catch (boost::bad_expression) {
        return "Invalid regular expression in substitution definition.";
    }
    return NULL;
}

/**
 * @brief Set the minimum and maximum number of threads
 * @param pParams miscellaneous data
 * @param pCfg user data for the directory/location
 * @param pMin the minimum number of threads
 * @param pMax the maximum number of threads
 * @return NULL if parameters are valid, otherwise a string describing the error
 */
const char*
setThreads(cmd_parms* pParams, void* pCfg, const char* pMin, const char* pMax) {
	size_t lMin, lMax;
	try {
		lMin = boost::lexical_cast<size_t>(pMin);
		lMax = boost::lexical_cast<size_t>(pMax);
	} catch (boost::bad_lexical_cast) {
		return "Invalid value(s) for minimum and maximum number of threads.";
	}

	if (pMax < pMin) {
		return "Invalid value(s) for minimum and maximum number of threads.";
	}

	gThreadPool->setThreads(lMin, lMax);
	return NULL;
}

/**
 * @brief Set the timeout for outgoing requests
 * @param pParams miscellaneous data
 * @param pCfg user data for the directory/location
 * @param pTimeout the timeout for outgoing requests in ms
 * @return NULL if parameters are valid, otherwise a string describing the error
 */
const char*
setTimeout(cmd_parms* pParams, void* pCfg, const char* pTimeout) {
	size_t lTimeout;
	try {
		lTimeout = boost::lexical_cast<unsigned int>(pTimeout);
	} catch (boost::bad_lexical_cast) {
		return "Invalid value(s) for timeout.";
	}

	gProcessor->setTimeout(lTimeout);
	return NULL;
}

/**
 * @brief Set the minimum and maximum queue size
 * @param pParams miscellaneous data
 * @param pCfg user data for the directory/location
 * @param pMin the minimum queue size
 * @param pMax the maximum queue size
 * @return NULL if parameters are valid, otherwise a string describing the error
 */
const char*
setQueue(cmd_parms* pParams, void* pCfg, const char* pMin, const char* pMax) {
	size_t lMin, lMax;
	try {
		lMin = boost::lexical_cast<size_t>(pMin);
		lMax = boost::lexical_cast<size_t>(pMax);
	} catch (boost::bad_lexical_cast) {
		return "Invalid value(s) for minimum and maximum queue size.";
	}

	if (pMax < pMin) {
		return "Invalid value(s) for minimum and maximum queue size.";
	}

	gThreadPool->setQueue(lMin, lMax);
	return NULL;
}

/**
 * @brief Add a substitution definition
 * @param pParams miscellaneous data
 * @param pCfg user data for the directory/location
 * @param pScope the scope of the substitution (HEADER, BODY, ALL)
 * @param pField the field on which to do the substitution
 * @param pMatch the regexp matching what should be replaced
 * @param pReplace the value which the match should be replaced with
 * @return NULL if parameters are valid, otherwise a string describing the error
 */
const char*
setSubstitution(cmd_parms* pParams, void* pCfg, tFilterBase::eFilterScope pScope, const char *pField, const char* pMatch, const char* pReplace) {
    const char *lErrorMsg = setActive(pParams, pCfg);
    if (lErrorMsg) {
        return lErrorMsg;
    }
    try {
        gProcessor->addSubstitution(pParams->path, pField, pMatch, pReplace, pScope);
    } catch (boost::bad_expression) {
        return "Invalid regular expression in substitution definition.";
    }
    return NULL;
}

// We need to create a few specialized versions of setSubstitution because we cannot pass more than 3 args to Apache conf commands
// This can be achieved with templates but it requires a more recent version of G++
const char*
setHeaderSubstitution(cmd_parms* pParams, void* pCfg, const char *pField, const char* pMatch, const char* pReplace) {
    return setSubstitution(pParams, pCfg, tFilterBase::HEADER, pField, pMatch, pReplace);
}

// We need to create a few specialized versions of setSubstitution because we cannot pass more than 3 args to Apache conf commands
// This can be achieved with templates but it requires a more recent version of G++
const char*
setBodySubstitution(cmd_parms* pParams, void* pCfg, const char *pField, const char* pMatch, const char* pReplace) {
    return setSubstitution(pParams, pCfg, tFilterBase::BODY, pField, pMatch, pReplace);
}

/**
 * @brief Activate duplication
 * @param pParams miscellaneous data
 * @param pCfg user data for the directory/location
 * @return NULL
 */
const char*
setActive(cmd_parms* pParams, void* pCfg) {
    struct DupConf **lConf = reinterpret_cast<DupConf **>(pCfg);
    if (!lConf) {
        return "No per_dir conf defined. This should never happen!";
    }
    // No dup conf struct initialized
    if (!*lConf) {
        *lConf = (DupConf *) apr_pcalloc(pParams->pool, sizeof(**lConf));
    }
    // No dir name initialized
    if (!((*lConf)->dirName)) {
        (*lConf)->dirName = (char *) apr_pcalloc(pParams->pool, sizeof(char) * (strlen(pParams->path) + 1));
        strcpy((*lConf)->dirName, pParams->path);
    }
    return NULL;
}

/**
 * @brief Add a filter definition
 * @param pParams miscellaneous data
 * @param pCfg user data for the directory/location
 * @param pField the field on which to do the substitution
 * @param pFilter a reg exp which has to match for this request to be duplicated
 * @return NULL if parameters are valid, otherwise a string describing the error
 */
const char*
setFilter(cmd_parms* pParams, void* pCfg, const char *pType, const char *pField, const char* pFilter) {
	const char *lErrorMsg = setActive(pParams, pCfg);
	if (lErrorMsg) {
            return lErrorMsg;
	}
	try {
            gProcessor->addFilter(pParams->path, pField, pFilter, tFilterBase::GetScopeFromString(pType));
	} catch (boost::bad_expression) {
		return "Invalid regular expression in filter definition.";
	} catch (std::exception) {
            return INVALID_SCOPE_VALUE;
        }
	return NULL;
}


const char*
setRawFilter(cmd_parms* pParams, void* pCfg, const char *pType, const char* pExpression) {
    const char *lErrorMsg = setActive(pParams, pCfg);
    if (lErrorMsg) {
        return lErrorMsg;
    }
    try {
        gProcessor->addRawFilter(pParams->path, pExpression, tFilterBase::GetScopeFromString(pType));
    } catch (boost::bad_expression) {
        return "Invalid regular expression in filter definition.";
    }
    catch (std::exception) {
        return INVALID_SCOPE_VALUE;
    }
    return NULL;
}

/**
 * @brief Clean up before the child exits
 */
apr_status_t
cleanUp(void *) {
	gThreadPool->stop();
	delete gThreadPool;
	gThreadPool = NULL;

	delete gProcessor;
	gProcessor = NULL;
	return APR_SUCCESS;
}

/**
 * @brief init curl and our own thread pool on child init
 * @param pPool the apache pool
 * @param pServer the apache server record
 */
void
childInit(apr_pool_t *pPool, server_rec *pServer) {
	curl_global_init(CURL_GLOBAL_ALL);
	gThreadPool->start();

	apr_pool_cleanup_register(pPool, NULL, cleanUp, cleanUp);
}


/** @brief Declaration of configuration commands */
command_rec gCmds[] = {
    // AP_INIT_(directive,
    //          function,
    //          void * extra data,
    //          overrides to allow in order to enable,
    //          help message),
	AP_INIT_TAKE1("DupDestination",
		reinterpret_cast<const char *(*)()>(&setDestination),
		0,
		OR_ALL,
		"Set the destination for the duplicated requests. Format: host[:port]"),
	AP_INIT_TAKE1("DupName",
		reinterpret_cast<const char *(*)()>(&setName),
		0,
		OR_ALL,
		"Set the program name for the stats log messages"),
	AP_INIT_TAKE1("DupUrlCodec",
		reinterpret_cast<const char *(*)()>(&setUrlCodec),
		0,
		OR_ALL,
		"Set the url enc/decoding style for url arguments (default or apache)"),
	AP_INIT_TAKE1("DupTimeout",
		reinterpret_cast<const char *(*)()>(&setTimeout),
		0,
		OR_ALL,
		"Set the timeout for outgoing requests in milliseconds."),
	AP_INIT_TAKE2("DupThreads",
		reinterpret_cast<const char *(*)()>(&setThreads),
		0,
		OR_ALL,
		"Set the minimum and maximum number of threads per pool."),
	AP_INIT_TAKE2("DupQueue",
		reinterpret_cast<const char *(*)()>(&setQueue),
		0,
		OR_ALL,
		"Set the minimum and maximum queue size for each thread pool."),
	AP_INIT_TAKE3("DupSubstitute",
		reinterpret_cast<const char *(*)()>(&setHeaderSubstitution),
		0,
		ACCESS_CONF,
		"Substitute part of the field in 1st argument matching the regexp in 2nd argument by the 3rd argument on requests before duplicating them. Substitutions are applied in the order they are defined in."),
	AP_INIT_TAKE3("DupBodySubstitute",
        reinterpret_cast<const char *(*)()>(&setBodySubstitution),
        0,
        ACCESS_CONF,
        "Substitute part of the field in 1st argument matching the regexp in 2nd argument by the 3rd argument on requests before duplicating them. Substitutions are applied in the order they are defined in."),
	AP_INIT_TAKE3("DupFilter",
		reinterpret_cast<const char *(*)()>(&setFilter),
		0,
		ACCESS_CONF,
		"Filter incoming request fields before duplicating them. "
		"If one or more filters are specified, at least one of them has to match."),
	AP_INIT_TAKE2("DupRawFilter",
		reinterpret_cast<const char *(*)()>(&setRawFilter),
		0,
		ACCESS_CONF,
		"Filter incoming request fields before duplicating them."
		"1st Arg: BODY HEAD ALL, data to match with the regex"
		"Simply performs a match with the specified REGEX."),
	AP_INIT_TAKE3("DupRawSubstitute",
		reinterpret_cast<const char *(*)()>(&setRawSubstitute),
		0,
		ACCESS_CONF,
		"Filter incoming request fields before duplicating them."
		"1st Arg: BODY HEAD ALL, data to match with the regex"
		"Simply performs a match with the specified REGEX."),
	AP_INIT_TAKE1("DupPayload",
		reinterpret_cast<const char *(*)()>(&setPayload),
		0,
		ACCESS_CONF,
		"Store the body part of the request"),
	AP_INIT_NO_ARGS("Dup",
		reinterpret_cast<const char *(*)()>(&setActive),
		0,
		ACCESS_CONF,
		"Duplicating requests on this location using the dup module. "
		"This is only needed if no filter or substitution is defined."),
        {0}
};

/**
 * @brief register hooks in apache
 * @param pPool the apache pool
 */
void
registerHooks(apr_pool_t *pPool) {
#ifndef UNIT_TESTING
    ap_hook_pre_config(preConfig, NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_post_config(postConfig, NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_child_init(&childInit, NULL, NULL, APR_HOOK_MIDDLE);
    ap_register_input_filter(gName, filterHandler, NULL, AP_FTYPE_CONTENT_SET);
#endif
}

} // End namespace

/// Apache module declaration
module AP_MODULE_DECLARE_DATA dup_module = {
    STANDARD20_MODULE_STUFF,
    DupModule::createDirConfig,
    0, // merge_dir_config
    0, // create_server_config
    0, // merge_server_config
    DupModule::gCmds,
    DupModule::registerHooks
};
