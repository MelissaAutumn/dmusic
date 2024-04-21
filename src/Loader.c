// Copyright © 2024. GothicKit Contributors
// SPDX-License-Identifier: MIT-Modern-Variant
#include "_Internal.h"

static void* DmLoader_resolveName(DmLoader* slf, char const* name, size_t* length);

DmResult DmLoader_create(DmLoader** slf, DmLoaderOptions opt) {
	if (slf == NULL) {
		return DmResult_INVALID_ARGUMENT;
	}

	DmLoader* new = *slf = Dm_alloc(sizeof *new);

	if (new == NULL) {
		return DmResult_MEMORY_EXHAUSTED;
	}

	new->reference_count = 1;
	new->autodownload = opt& DmLoader_DOWNLOAD;

	DmResolverList_init(&new->resolvers);
	DmStyleCache_init(&new->style_cache);
	DmDlsCache_init(&new->dls_cache);

	return DmResult_SUCCESS;
}

DmLoader* DmLoader_retain(DmLoader* slf) {
	if (slf == NULL) {
		return NULL;
	}

	(void) atomic_fetch_add(&slf->reference_count, 1);
	return slf;
}

void DmLoader_release(DmLoader* slf) {
	if (slf == NULL) {
		return;
	}

	size_t refs = atomic_fetch_sub(&slf->reference_count, 1) - 1;
	if (refs != 0) {
		return;
	}

	DmStyleCache_free(&slf->style_cache);
	DmDlsCache_free(&slf->dls_cache);
	DmResolverList_free(&slf->resolvers);
	Dm_free(slf);
}

DmResult DmLoader_addResolver(DmLoader* slf, DmLoaderResolverCallback* resolve, void* ctx) {
	if (slf == NULL || resolve == NULL) {
		return DmResult_INVALID_ARGUMENT;
	}

	DmResolver resolver;
	resolver.context = ctx;
	resolver.resolve = resolve;

	return DmResolverList_add(&slf->resolvers, resolver);
}

void* DmLoader_resolveName(DmLoader* slf, const char* name, size_t* length) {
	for (size_t i = 0U; i < slf->resolvers.length; ++i) {
		DmResolver* resolver = &slf->resolvers.data[i];
		void* bytes = resolver->resolve(resolver->context, name, length);
		if (bytes != NULL) {
			return bytes;
		}
	}
	return NULL;
}

DmResult DmLoader_getSegment(DmLoader* slf, char const* name, DmSegment** segment) {
	if (slf == NULL || name == NULL || segment == NULL) {
		return DmResult_INVALID_ARGUMENT;
	}

	size_t length = 0;
	void* bytes = DmLoader_resolveName(slf, name, &length);
	if (bytes == NULL) {
		return DmResult_NOT_FOUND;
	}

	DmResult rv = DmSegment_create(segment);
	if (rv != DmResult_SUCCESS) {
		Dm_free(bytes);
		return rv;
	}

	rv = DmSegment_parse(*segment, bytes, length);
	if (rv != DmResult_SUCCESS) {
		DmSegment_release(*segment);
		return rv;
	}

	if (!slf->autodownload) {
		return DmResult_SUCCESS;
	}

	rv = DmSegment_download(*segment, slf);
	if (rv != DmResult_SUCCESS) {
		DmSegment_release(*segment);
		return rv;
	}

	return DmResult_SUCCESS;
}

static char* Dm_utf16ToUtf8(char16_t const* u16) {
	size_t len = 0;
	while (u16[len] != 0) {
		len += 1;
	}

	// A UTF code point can be represented by up to 4 UTF-8 bytes
	char* name = Dm_alloc(len * 4 + 1);

	mbstate_t state;
	memset(&state, 0, sizeof state);

	size_t i = 0;
	size_t j = 0;
	for (; i < len; ++i) {
		j += c16rtomb(name + j, u16[i], &state);
	}

	name[j] = '\0';
	return name;
}

DmResult DmLoader_getDownloadableSound(DmLoader* slf, DmReference const* ref, DmDls** snd) {
	if (slf == NULL || ref == NULL || snd == NULL) {
		return DmResult_INVALID_ARGUMENT;
	}

	// See if we have the requested item in the cache.
	for (size_t i = 0; i < slf->dls_cache.length; ++i) {
		if (!DmGuid_equals(&ref->guid, &slf->dls_cache.data[i]->guid)) {
			continue;
		}

		*snd = DmDls_retain(slf->dls_cache.data[i]);
		return DmResult_SUCCESS;
	}

	// Convert `file` from UTF-16 to UTF-8
	char* name = Dm_utf16ToUtf8(ref->file);

	// Resolve and parse the DLS
	size_t length = 0;
	void* bytes = DmLoader_resolveName(slf, name, &length);
	if (bytes == NULL) {
		Dm_free(name);
		return DmResult_NOT_FOUND;
	}

	Dm_free(name);

	DmResult rv = DmDls_create(snd);
	if (rv != DmResult_SUCCESS) {
		Dm_free(bytes);
		return rv;
	}

	rv = DmDls_parse(*snd, bytes, length);
	if (rv != DmResult_SUCCESS) {
		DmDls_release(*snd);
		return rv;
	}

	rv = DmDlsCache_add(&slf->dls_cache, DmDls_retain(*snd));
	return rv;
}

DmResult DmLoader_getStyle(DmLoader* slf, DmReference const* ref, DmStyle** sty) {
	if (slf == NULL || ref == NULL || sty == NULL) {
		return DmResult_INVALID_ARGUMENT;
	}

	// See if we have the requested item in the cache.
	for (size_t i = 0; i < slf->style_cache.length; ++i) {
		if (!DmGuid_equals(&ref->guid, &slf->style_cache.data[i]->guid)) {
			continue;
		}

		*sty = DmStyle_retain(slf->style_cache.data[i]);
		return DmResult_SUCCESS;
	}

	char* name = Dm_utf16ToUtf8(ref->file);

	// Resolve and parse the DLS
	size_t length = 0;
	void* bytes = DmLoader_resolveName(slf, name, &length);
	if (bytes == NULL) {
		Dm_free(name);
		return DmResult_NOT_FOUND;
	}

	Dm_free(name);

	DmResult rv = DmStyle_create(sty);
	if (rv != DmResult_SUCCESS) {
		Dm_free(bytes);
		return rv;
	}

	rv = DmStyle_parse(*sty, bytes, length);
	if (rv != DmResult_SUCCESS) {
		DmStyle_release(*sty);
		return rv;
	}

	rv = DmStyleCache_add(&slf->style_cache, DmStyle_retain(*sty));
	return rv;
}
