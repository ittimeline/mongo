/**
 *    Copyright (C) 2018-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#pragma once

#include "mongo/db/fts/fts_phrase_matcher.h"
#include "mongo/db/fts/unicode/codepoints.h"

#include <string>

namespace mongo {
namespace fts {

class FTSLanguage;

/**
 * UnicodeFTSPhraseMatcher
 *
 * A phrase matcher that looks for exact substring matches that ignore diacritics, and with UTF-8
 * aware case folding if the phrase match is not specified as case sensitive. Optionally, the phrase
 * matching can be diacritic sensitive if a parameter is passed to the constructor. Additionally, if
 * the language string passed to the phrase matcher's constructor is Turkish (uses the special I
 * case fold mapping), the phrase matcher will take that into account.
 */
class UnicodeFTSPhraseMatcher final : public FTSPhraseMatcher {
    UnicodeFTSPhraseMatcher(const UnicodeFTSPhraseMatcher&) = delete;
    UnicodeFTSPhraseMatcher& operator=(const UnicodeFTSPhraseMatcher&) = delete;

public:
    UnicodeFTSPhraseMatcher(const std::string& language);

    bool phraseMatches(const std::string& phrase,
                       const std::string& haystack,
                       Options options) const override;

private:
    unicode::CaseFoldMode _caseFoldMode;
};

}  // namespace fts
}  // namespace mongo
