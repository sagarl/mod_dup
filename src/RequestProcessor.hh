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

#include <boost/regex.hpp>
#include <boost/scoped_ptr.hpp>
#include <string>
#include <map>
#include <apr_pools.h>

#include "MultiThreadQueue.hh"
#include "RequestInfo.hh"
#include "UrlCodec.hh"


namespace DupModule {

    typedef std::pair<std::string, std::string> tKeyVal;

    /**
     * Base class for filters and substitutions
     */
    struct tFilterBase{

        /**
         * Scopes that a filter/sub can have
         */
        enum eFilterScope{
            ALL = 0x3,
            HEADER = 0x1,
            BODY = 0x2,
        };

        typedef enum eFilterScope eFilterScope;

        tFilterBase(const std::string &regex, eFilterScope scope);

        /**
         * Translates the character value of a scope into it's enumerate value
         * raises a std::exception if the string doesn't match any predefined values
         * Values are : ALL, BODY, HEADER
         */
        static eFilterScope GetScopeFromString(const char*);

        eFilterScope mScope; /** The action of the filter */
        boost::regex mRegex; /** The matching regular expression */
    };

    /**
     * Represents a filter that applies on a key
     */
    struct tFilter : public tFilterBase{

        tFilter(const std::string &regex, eFilterScope scope);

        std::string mField; /** The key or field the filter applies on */
    };

    /**
     * Represents a raw substitution
     */
    struct tSubstitute : public tFilterBase{

        tSubstitute(const std::string &regex,
                      const std::string &replacement, eFilterScope scope);

        std::string mReplacement; /** The replacement value regex */
    };

    /** @brief Maps a path to a substitution. Not a multimap because order matters. */
    typedef std::map<std::string, std::list<tSubstitute> > tFieldSubstitutionMap;


    /** @brief A container for the filter and substituion commands */
    struct tRequestProcessorCommands {

        /** @brief The list of filter commands
         * Indexed by the field on which they apply
         */
        std::multimap<std::string, tFilter> mFilters;

        /** @brief The substition maps */
        tFieldSubstitutionMap mSubstitutions;

        /** @brief The Raw filter list */
        std::list<tFilter> mRawFilters;

        /** @brief The Raw Substitution list */
        std::list<tSubstitute> mRawSubstitutions;
    };

    /**
     * @brief RequestProcessor is responsible for processing and sending requests to their destination.
     * This is where all the business logic is configured and executed.
     * Its main method is run which will continuously pull requests off the internal queue in order to process them.
     */
    class RequestProcessor
    {
    private:
	/** @brief Maps paths to their corresponding processing (filter and substitution) directives */
	std::map<std::string, tRequestProcessorCommands> mCommands;
	/** @brief The destination string for the duplicated requests with the following format: <host>[:<port>] */
	std::string mDestination;
	/** @brief The timeout for outgoing requests in ms */
	unsigned int mTimeout;
	/** @brief The number of requests which timed out */
	volatile unsigned int mTimeoutCount;
        /** @brief The number of requests duplicated */
        volatile unsigned int mDuplicatedCount;
		/** @brief The url codec */
		boost::scoped_ptr<const IUrlCodec> mUrlCodec;
		

    public:
	/**
	 * @brief Constructs a RequestProcessor
	 */
	RequestProcessor() : mTimeout(0), mTimeoutCount(0), mDuplicatedCount(0) {
		setUrlCodec();
	}

	/**
	 * @brief Set the destination server and port
	 * @param pDestination the destination in &lt;host>[:&lt;port>] format
	 */
	void
	setDestination(const std::string &pDestination);

	/**
	 * @brief Set the timeout
	 * @param pTimeout the timeout in ms
	 */
	void
	setTimeout(const unsigned int &pTimeout);

	/**
	 * @brief Get the number of requests which timed out since last call to this method
	 * @return The timeout count
	 */
	const unsigned int
	getTimeoutCount();

        /**
         * @brief Get the number of requests duplicated since last call to this method
         * @return The duplicated count
         */
        const unsigned int
        getDuplicatedCount();

		/**
		 * @brief Set the url codec
		 * @param pUrlCodec the codec to use
		 */
		void
		setUrlCodec(const std::string &pUrlCodec="default");

        /**
         * @brief Add a filter for all requests on a given path
         * @param pPath the path of the request
         * @param pField the field on which to do the substitution
         * @param pFilter a reg exp which has to match for this request to be duplicated
         */
        void
        addFilter(const std::string &pPath, const std::string &pField, const std::string &pFilter, tFilterBase::eFilterScope scope);

        /**
         * @brief Add a RAW filter for all requests on a given path
         * @param pPath the path of the request
         * @param pFilter a reg exp which has to match for this request to be duplicated
         * @param Scope: the elements to match the filter with
         */
        void
        addRawFilter(const std::string &pPath, const std::string &pFilter, tFilterBase::eFilterScope scope);

        /**
         * @brief Schedule a substitution on the value of a given field of all requests on a given path
         * @param pPath the path of the request
         * @param pField the field on which to do the substitution
         * @param pMatch the regexp matching what should be replaced
         * @param pReplace the value which the match should be replaced with
         */
        void
        addSubstitution(const std::string &pPath, const std::string &pField,
                        const std::string &pMatch, const std::string &pReplace, tFilterBase::eFilterScope scope);

        /**
         * @brief Schedule a Raw substitution on the value of all requests on a given path
         * @param pPath the path of the request
         * @param pField the field on which to do the substitution
         * @param pMatch the regexp matching what should be replaced
         * @param pReplace the value which the match should be replaced with
         */
        void
        addRawSubstitution(const std::string &pPath, const std::string &pMatch, const std::string &pReplace, tFilterBase::eFilterScope scope);

        /**
         * @brief Returns wether or not the arguments match any of the filters
         * @param pParsedArgs the list with the argument key value pairs
         * @param pFilters the filters which should be applied
         * @return true if there are no filters or at least one filter matches, false otherwhise
         */
        bool
        argsMatchFilter(RequestInfo &pRequest, tRequestProcessorCommands &pCommands, std::list<tKeyVal> &pParsedArgs);

        /**
         * @brief Parses arguments into key valye pairs. Also url-decodes values and converts keys to upper case.
         * @param pParsedArgs the list which should be filled with the key value pairs
         * @param pArgs the parameters part of the query
         */
        void
        parseArgs(std::list<tKeyVal> &pParsedArgs, const std::string &pArgs);

        /**
         * @brief Process a field. This includes filtering and executing substitutions
         * @param pConfPath the path of the configuration which is applied
         * @param pArgs the HTTP arguments/parameters of the incoming request
         * @return true if the request should get duplicated, false otherwise.
         * If and only if it returned true, pArgs will have all necessary substitutions applied.
         */
        bool
        processRequest(const std::string &pConfPath, RequestInfo &pRequest);

        /**
         * @brief Run the infinite loop which pops new requests of the given queue, processes them and sends the over to the configured destination
         * @param pQueue the queue which gets filled with incoming requests
         */
        void
        run(MultiThreadQueue<RequestInfo> &pQueue);

    private:

        bool
        substituteRequest(RequestInfo &pRequest, tRequestProcessorCommands &pCommands, std::list<tKeyVal> &pHeaderParsedArgs);

        bool
        keyFilterMatch(std::multimap<std::string, tFilter> &pFilters, std::list<tKeyVal> &pParsedArgs, tFilterBase::eFilterScope scope);

        bool
        keySubstitute(tFieldSubstitutionMap &pSubs,
                      std::list<tKeyVal> &pParsedArgs,
                      tFilterBase::eFilterScope scope,
                      std::string &result);
    };
}
