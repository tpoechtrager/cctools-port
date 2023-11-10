/* -*- mode: C++; c-basic-offset: 4; tab-width: 4 -*- 
 *
 * Copyright (c) 2008 Apple Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */
#include <vector>
#include <algorithm>

#include "ExportsTrie.h"
#include "MachOTrie.hpp"
#include "prune_trie.h"


/*
 * prune_trie() is a C vended function that is used by strip(1) to prune out
 * defined exported symbols from the export trie.  It is passed a pointer to
 * the start of bytes of the the trie and the size.  The prune() funciton
 * passed is called with each symbol name in the trie to determine if it is
 * to be pruned (retuning 1) or not (returning 0).  It writes the new trie
 * back into the trie buffer and returns the new size in trie_new_size.
 * If the pruning succeeds, NULL is returned.  If there was an error processing
 * the trie (e.f. it is malformed), then an error message string is returned.
 * The error string can be freed.
 */
const char*
prune_trie(
	uint8_t*	trie_start,
	uint32_t	trie_start_size,
	int			(*prune)(const char *name),
	uint32_t*	trie_new_size)
{
	// convert trie to vector of entries
	std::vector<mach_o::trie::Entry> exports;
	try {
		parseTrie(trie_start, trie_start+trie_start_size, exports);
	}
	catch (const char* msg) {
		return strdup(msg);
	}
	catch (...) {
		return strdup("unexpected exception processing trie");
	}
	
	// prune entries into new vector of entries
	auto pruneStartIt = std::remove_if(exports.begin(), exports.end(), [prune](const mach_o::trie::Entry& entry) {
		return prune(entry.name) != 0;
	});
	// early return when none of the exports were pruned
	if (pruneStartIt == exports.end()) {
		*trie_new_size = trie_start_size;
		return nullptr;
	}
	exports.erase(pruneStartIt, exports.end());

	// create new export trie
	mach_o::ExportsTrie trie(exports.size(), ^(size_t index) {
		const mach_o::trie::Entry& oldExport = exports[index];
		mach_o::ExportsTrie::Export newExport;
		newExport.name = oldExport.name;
		newExport.flags = oldExport.flags;
		newExport.offset = oldExport.address;
		newExport.other = oldExport.other;
		if ( oldExport.importName != nullptr ) {
			newExport.importName = oldExport.importName;
		}

		return newExport;
	});
	if ( mach_o::Error err = std::move(trie.buildError()) )
		return strdup(err.message());


	uint8_t trieAlign = 0;
	uint8_t triePadding = 0;
	// Need to align trie to 8 or 4 bytes.  We don't know the arch, but if the incoming trie
	// was not 8-byte aligned, then it can't be a 64-bit arch, so use 4-byte alignement.
	if ( (trie_start_size % 8) != 0 ) {
		trieAlign = 4;
	} else {
		trieAlign = 8;
	}
	size_t					trieSize = 0;
	const uint8_t*	trieBytes = trie.bytes(trieSize);
	if ( (trieSize % trieAlign) != 0 ) {
		triePadding = trieAlign - (trieSize % trieAlign);
	}
	
	// copy into place, zero pad
	*trie_new_size = trieSize + triePadding;
	if ( *trie_new_size > trie_start_size ) {
		char* msg;
		asprintf(&msg, "new trie is larger (%d) than original (%d)", *trie_new_size, trie_start_size);
		return msg;
	}
	memcpy(trie_start, trieBytes, trieSize);
	bzero(trie_start + trieSize, trie_start_size - trieSize);

	// success
	return nullptr;
}
