// Copyright © 2024. GothicKit Contributors
// SPDX-License-Identifier: MIT-Modern-Variant
#include "_Internal.h"

DmResult DmBand_create(DmBand** slf) {
	if (slf == NULL) {
		return DmResult_INVALID_ARGUMENT;
	}

	DmBand* new = *slf = Dm_alloc(sizeof *new);
	if (new == NULL) {
		return DmResult_MEMORY_EXHAUSTED;
	}

	new->reference_count = 1;
	return DmResult_SUCCESS;
}

DmBand* DmBand_retain(DmBand* slf) {
	if (slf == NULL) {
		return NULL;
	}

	(void) atomic_fetch_add(&slf->reference_count, 1);
	return slf;
}

void DmBand_release(DmBand* slf) {
	if (slf == NULL) {
		return;
	}

	size_t count = atomic_fetch_sub(&slf->reference_count, 1) - 1;
	if (count > 0) {
		return;
	}

	for (size_t i = 0; i < slf->instrument_count; ++i) {
		DmInstrument_free(&slf->instruments[i]);
	}

	Dm_free(slf->instruments);
	Dm_free(slf);
}

static DmDlsInstrument* DmBand_findDlsInstrument(DmInstrument* slf, DmDls* dls) {
	uint32_t bank = (slf->patch & 0xFF00U) >> 8;
	uint32_t patch = slf->patch & 0xFFU;

	DmDlsInstrument* ins = NULL;
	for (size_t i = 0; i < dls->instrument_count; ++i) {
		ins = &dls->instruments[i];

		// TODO(lmichaelis): We need to ignore drum kits for now since I don't know how to handle them properly
		if (ins->bank & DmDls_DRUM_KIT) {
			Dm_report(DmLogLevel_DEBUG, "DmBand: Ignoring DLS drum-kit instrument '%s'", ins->info.inam);
			continue;
		}

		// If it's the correct instrument, return it.
		if (ins->bank == bank && ins->patch == patch) {
			return ins;
		}
	}

	Dm_report(DmLogLevel_WARN,
	          "DmBand: Instrument patch %d:%d not found in band '%s'",
	          bank,
	          patch,
	          slf->reference.name);
	return NULL;
}

DmResult DmBand_download(DmBand* slf, DmLoader* loader) {
	if (slf == NULL || loader == NULL) {
		return DmResult_INVALID_ARGUMENT;
	}

	Dm_report(DmLogLevel_INFO, "DmBand: Downloading instruments for band '%s'", slf->info.unam);

	DmResult rv = DmResult_SUCCESS;
	for (size_t i = 0; i < slf->instrument_count; ++i) {
		DmInstrument* instrument = &slf->instruments[i];

		// The DLS has already been downloaded. We don't need to do it again.
		if (instrument->dls != NULL) {
			continue;
		}

		// If the patch is not valid, this instrument cannot be played since we don't know
		// where to find it in the DLS collection.
		if (!(instrument->flags & DmInstrument_PATCH)) {
			Dm_report(DmLogLevel_DEBUG,
			          "DmBand: Not downloading instrument '%s' without valid patch",
			          instrument->reference.name);
			continue;
		}

		// TODO(lmichaelis): The General MIDI and Roland GS collections are not supported.
		if (instrument->flags & (DmInstrument_GS | DmInstrument_GM)) {
			Dm_report(DmLogLevel_INFO,
			          "DmBand: Cannot download instrument '%s': GS and GM collections not available",
			          instrument->reference.name);
			continue;
		}

		rv = DmLoader_getDownloadableSound(loader, &instrument->reference, &instrument->dls_collection);
		if (rv != DmResult_SUCCESS) {
			break;
		}

		// Locate and store the referenced DLS-instrument
		instrument->dls = DmBand_findDlsInstrument(instrument, instrument->dls_collection);
		if (instrument->dls == NULL) {
			continue;
		}

		Dm_report(DmLogLevel_DEBUG,
		          "DmBand: DLS instrument '%s' assigned to channel %d for band '%s'",
		          instrument->dls->info.inam,
		          instrument->channel,
		          slf->info.unam);
	}

	return rv;
}

bool DmBand_isSortOfSameAs(DmBand* slf, DmBand* oth) {
	if (slf == oth) {
		return true;
	}

	if (slf == NULL || oth == NULL) {
		return false;
	}

	if (slf->instrument_count != oth->instrument_count) {
		return false;
	}

	for (size_t i = 0; i < oth->instrument_count; ++i) {
		if (slf->instruments[i].dls != oth->instruments[i].dls) {
			return false;
		}
	}

	return true;
}

void DmInstrument_free(DmInstrument* slf) {
	if (slf == NULL || slf->dls == NULL) {
		return;
	}

	DmDls_release(slf->dls_collection);
	slf->dls = NULL;
	slf->dls_collection = NULL;
}
