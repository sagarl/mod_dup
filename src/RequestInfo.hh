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

#include <string>

namespace DupModule {


    /**
     * @brief Contains information about the incoming request.
     */
    class RequestInfo {
    private:
	/** @brief True if the request processor should stop ater seeing this object. */
	bool mPoison;

    public:
	/** @brief The location (in the conf) which matched this query. */
	std::string mConfPath;
	/** @brief The path part of the request. */
	std::string mPath;
	/** @brief The parameters part of the query (without leading ?). */
	std::string mArgs;
	/** @brief The body part of the query */
	std::string mBody;

	/**
	 * @brief Constructs the object using the three strings.
	 * @param pConfPath The location (in the conf) which matched this query
	 * @param pPath The path part of the request
	 * @param pConfPath The parameters part of the query (without leading ?)
	 */
        RequestInfo(const std::string &pConfPath, const std::string &pPath, const std::string &pArgs, const std::string *body = 0);

	/**
	 * @brief Constructs a poisonous object causing the processor to stop when read
	 */
	RequestInfo();

        /**
         * returns true if the request has a body
         */
        bool hasBody() const;

	/**
	 * @brief Returns wether the the request is poisonous
	 * @return true if poisonous, false otherwhise
	 */
	bool isPoison();
    };

    static const RequestInfo POISON_REQUEST;
}
