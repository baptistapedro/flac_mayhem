
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
//#include <string>



#include "compat.h"
#include "FLAC/metadata.h"
#include "FLAC/stream_encoder.h"
#include "FLAC/assert.h"

static void progress_callback(const FLAC__StreamEncoder *encoder, FLAC__uint64 bytes_written, FLAC__uint64 samples_written, unsigned frames_written, unsigned total_frames_estimate, void *client_data);

#define READSIZE 1024

static unsigned total_samples = 0; /* can use a 32-bit number due to WAVE size limitations */
static FLAC__byte buffer[READSIZE/*samples*/ * 2/*bytes_per_sample*/ * 2/*channels*/]; /* we read the WAVE data into here */
static FLAC__int32 pcm[READSIZE/*samples*/ * 2/*channels*/];

/* ENCODER CALLBACKS */
void progress_callback(const FLAC__StreamEncoder *encoder, FLAC__uint64 bytes_written, FLAC__uint64 samples_written, unsigned frames_written, unsigned total_frames_estimate, void *client_data)
{
	(void)encoder, (void)client_data;
	fprintf(stderr, "wrote %lu bytes, %lu samples, total frames %u, %u frames written, %u total frames estimate\n", bytes_written, samples_written, total_samples, frames_written, total_frames_estimate);
	
}

/* DECODER CALLBACKS  AND GLOBALS */
static FLAC__StreamDecoderWriteStatus write_callback(const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame, const FLAC__int32 * const buffer[], void *client_data);
static void metadata_callback(const FLAC__StreamDecoder *decoder, const FLAC__StreamMetadata *metadata, void *client_data);
static void error_callback(const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status, void *client_data);

static FLAC__uint64 decoder_total_samples = 0;
static unsigned sample_rate = 0;
static unsigned channels = 0;
static unsigned bps = 0;
static FLAC__bool write_little_endian_uint16(FILE *f, FLAC__uint16 x)
{
	return
		fputc(x, f) != EOF &&
		fputc(x >> 8, f) != EOF
	;
}

static FLAC__bool write_little_endian_int16(FILE *f, FLAC__int16 x)
{
	return write_little_endian_uint16(f, (FLAC__uint16)x);
}

static FLAC__bool write_little_endian_uint32(FILE *f, FLAC__uint32 x)
{
	return
		fputc(x, f) != EOF &&
		fputc(x >> 8, f) != EOF &&
		fputc(x >> 16, f) != EOF &&
		fputc(x >> 24, f) != EOF
	;
}

FLAC__StreamDecoderWriteStatus write_callback(const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame, const FLAC__int32 * const buffer[], void *client_data) {
	(void)decoder;
	return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;

}
void metadata_callback(const FLAC__StreamDecoder *decoder, const FLAC__StreamMetadata *metadata, void *client_data)
{
	(void)decoder, (void)client_data;

	/* print some stats */
	if(metadata->type == FLAC__METADATA_TYPE_STREAMINFO) {
		/* save for later */
		decoder_total_samples = metadata->data.stream_info.total_samples;
		sample_rate = metadata->data.stream_info.sample_rate;
		channels = metadata->data.stream_info.channels;
		bps = metadata->data.stream_info.bits_per_sample;

		fprintf(stderr, "sample rate    : %u Hz\n", sample_rate);
		fprintf(stderr, "channels       : %u\n", channels);
		fprintf(stderr, "bits per sample: %u\n", bps);
		fprintf(stderr, "total samples  : %lu\n", decoder_total_samples);
	}
}
void error_callback(const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status, void *client_data)
{
	(void)decoder, (void)client_data;

	fprintf(stderr, "Got error callback: %s\n", FLAC__StreamDecoderErrorStatusString[status]);
}


static char *strdup_or_die_(const char *s)
{
	char *x = strdup(s);
	if(0 == x) {
		fprintf(stderr, "ERROR: out of memory copying string \"%s\"\n", s);
		exit(1);
	}
	return x;
}

int fuzzme(char *jpegFile) {
	FLAC__bool ok = true;
	FLAC__StreamEncoder *encoder = 0;
	FLAC__StreamEncoderInitStatus init_status;
	FLAC__StreamMetadata *metadata[3];
	FLAC__StreamMetadata_VorbisComment_Entry entry;
	FILE *fin;
	unsigned sample_rate = 0;
	unsigned channels = 0;
	unsigned bps = 0;


	if((fin = fopen("sampleWav.wav" /*argv[1]*/, "rb")) == NULL) {
		fprintf(stderr, "ERROR: Opening Wav file\n");
		return 1;
	}

	// Read the wav file, will not mutate
	if(
		fread(buffer, 1, 44, fin) != 44 ||
		memcmp(buffer, "RIFF", 4) ||
		memcmp(buffer+8, "WAVEfmt \020\000\000\000\001\000\002\000", 16) ||
		memcmp(buffer+32, "\004\000\020\000data", 8)
	) {
		fprintf(stderr, "ERROR: invalid/unsupported WAVE file, only 16bps stereo WAVE in canonical form allowed\n");
		fclose(fin);
		return 1;
	}
	sample_rate = ((((((unsigned)buffer[27] << 8) | buffer[26]) << 8) | buffer[25]) << 8) | buffer[24];
	channels = 2;
	bps = 16;
	total_samples = (((((((unsigned)buffer[43] << 8) | buffer[42]) << 8) | buffer[41]) << 8) | buffer[40]) / 4;

	/* Allocate the encoder */
	if((encoder = FLAC__stream_encoder_new()) == NULL) {
		fprintf(stderr, "ERROR: allocating encoder\n");
		fclose(fin);
		return 1;
	}

	ok &= FLAC__stream_encoder_set_verify(encoder, true);
	ok &= FLAC__stream_encoder_set_compression_level(encoder, 5);
	ok &= FLAC__stream_encoder_set_channels(encoder, channels);
	ok &= FLAC__stream_encoder_set_bits_per_sample(encoder, bps);
	ok &= FLAC__stream_encoder_set_sample_rate(encoder, sample_rate);
	ok &= FLAC__stream_encoder_set_total_samples_estimate(encoder, total_samples);

	/* Add metadata tags including padding block */
	if(ok) {
		if ((metadata[0] = FLAC__metadata_object_new(FLAC__METADATA_TYPE_VORBIS_COMMENT)) == NULL) {
			fprintf(stderr, "ERROR: FLAC__metadata_object_new(FLAC__METADATA_TYPE_VORBIS_COMMENT)\n");
			ok = false;
		}
		
		if ((metadata[1] = FLAC__metadata_object_new(FLAC__METADATA_TYPE_PICTURE)) == NULL) {
			fprintf(stderr, "ERROR: FLAC__metadata_object_new(FLAC__METADATA_TYPE_PICTURE)\n");
			ok = false;
		}
		
		if ((metadata[2] = FLAC__metadata_object_new(FLAC__METADATA_TYPE_PADDING)) == NULL ) {
			fprintf(stderr, "ERROR: FLAC__metadata_object_new(FLAC__METADATA_TYPE_PICTURE)\n");
			ok = false;
		}	
		if (!FLAC__metadata_object_vorbiscomment_entry_from_name_value_pair(&entry, "ARTIST", "Some Artist")) {
			fprintf(stderr, "ERROR: FLAC__metadata_object_vorbiscomment_entry_from_name_value_pair\n");
			ok = false;
		}
			
		if (!FLAC__metadata_object_vorbiscomment_append_comment(metadata[0], entry, false /*copy*/)) {
			fprintf(stderr, "ERROR: FLAC__metadata_object_vorbiscomment_append_comment\n");
			ok = false;
		}
		
		metadata[1]->is_last = false;
		metadata[1]->type = FLAC__METADATA_TYPE_PICTURE;
		metadata[1]->length =
			(
				FLAC__STREAM_METADATA_PICTURE_TYPE_LEN +
				FLAC__STREAM_METADATA_PICTURE_MIME_TYPE_LENGTH_LEN +
				FLAC__STREAM_METADATA_PICTURE_DESCRIPTION_LENGTH_LEN + 
				FLAC__STREAM_METADATA_PICTURE_WIDTH_LEN +
				FLAC__STREAM_METADATA_PICTURE_HEIGHT_LEN +
				FLAC__STREAM_METADATA_PICTURE_DEPTH_LEN +
				FLAC__STREAM_METADATA_PICTURE_COLORS_LEN +
				FLAC__STREAM_METADATA_PICTURE_DATA_LENGTH_LEN ) / 8;

		metadata[1]->data.picture.type = FLAC__STREAM_METADATA_PICTURE_TYPE_FRONT_COVER;
		metadata[1]->data.picture.mime_type = strdup_or_die_("image/jpeg");
		metadata[1]->length += strlen(metadata[1]->data.picture.mime_type);
		metadata[1]->data.picture.description = (FLAC__byte*)strdup_or_die_("desc");
		metadata[1]->length += strlen((const char *)metadata[1]->data.picture.description);
		metadata[1]->data.picture.width = 32; // must
		metadata[1]->data.picture.height = 32; //must
		metadata[1]->data.picture.depth = 24;
		metadata[1]->data.picture.colors = 0;
		
		// Read data from file seed
		FILE *f = fopen(jpegFile, "rb");
		char *buf;
		if (!f) {
			fprintf(stderr, "PICTURE-ERROR: Cant fopen() jpeg file\n");
			return 1;
		}
		fseek(f, 0, SEEK_END);
		metadata[1]->data.picture.data_length = ftell(f);
		fseek(f, 0, SEEK_SET);
	
		printf("pic datalen -> %u\n", metadata[1]->data.picture.data_length);
		
		buf = (char *)malloc(metadata[1]->data.picture.data_length);
		if (!buf) {
			fprintf(stderr, "PICTURE-ERROR: failed malloc jpeg data\n");
			return 1;
		}
		
		if(fread(buf, 1, metadata[1]->data.picture.data_length, f) !=
			metadata[1]->data.picture.data_length) {
			fprintf(stderr, "PICTURE-ERROR: Cant fread() jpeg file\n");
			free(buf);
			fclose(f);	
			return 1;
		}
		
		// Target fuzz method
		if (!FLAC__metadata_object_picture_set_data(metadata[1], (FLAC__byte*)buf, 
			metadata[1]->data.picture.data_length , /*copy=*/false)) {
			fprintf(stderr, "PICTURE-ERROR: picture_set_data\n");
		}
		metadata[1]->length += metadata[1]->data.picture.data_length; // Important: increase block len
		printf("data.picture ->  %s\n", metadata[1]->data.picture.data);
	
	
		// Padding Block	
		metadata[2]->length = 1234; // padding length
		
		ok = FLAC__stream_encoder_set_metadata(encoder, metadata, 3);
	}

	// Init encoder
	const char *flacFileWithJpeg = "flacFileWithJpeg.flac"; // Output file
	if(ok) {
		init_status = FLAC__stream_encoder_init_file(encoder, /*argv[2]*/ flacFileWithJpeg, progress_callback, /*client_data=*/NULL);
		if(init_status != FLAC__STREAM_ENCODER_INIT_STATUS_OK) {
			fprintf(stderr, "ERROR: initializing encoder: %s\n", FLAC__StreamEncoderInitStatusString[init_status]);
			ok = false;
		}
	}

	// Read blocks of samples from WAVE file and feed to encoder 
	if(ok) {
		size_t left = (size_t)total_samples;
		while(ok && left) {
			size_t need = (left>READSIZE? (size_t)READSIZE : (size_t)left);
			if(fread(buffer, channels*(bps/8), need, fin) != need) {
				fprintf(stderr, "ERROR: reading from WAVE file\n");
				ok = false;
			}
			else {
				/* convert the packed little-endian 16-bit PCM samples from WAVE into an interleaved FLAC__int32 buffer for libFLAC */
				size_t i;
				for(i = 0; i < need*channels; i++) {
					/* inefficient but simple and works on big- or little-endian machines */
					pcm[i] = (FLAC__int32)(((FLAC__int16)(FLAC__int8)buffer[2*i+1] << 8) | (FLAC__int16)buffer[2*i]);
				}
				/* feed samples to encoder */
				ok = FLAC__stream_encoder_process_interleaved(encoder, pcm, need);
			}
			left -= need;
		}
	}

	ok &= FLAC__stream_encoder_finish(encoder);

	fprintf(stderr, "encoding: %s\n", ok? "succeeded" : "FAILED");
	fprintf(stderr, "   state: %s\n", FLAC__StreamEncoderStateString[FLAC__stream_encoder_get_state(encoder)]);

	/* now that encoding is finished, the metadata can be freed */
	FLAC__metadata_object_delete(metadata[0]);
	FLAC__metadata_object_delete(metadata[1]);
	FLAC__metadata_object_delete(metadata[2]);
	
	FLAC__stream_encoder_delete(encoder);
	fclose(fin);

	/* DECODE PHASE */
	printf("\n[***] DECODE PHASE\n");

	FLAC__bool decodeOk = true;
	FLAC__StreamDecoder *decoder = 0;
	FLAC__StreamDecoderInitStatus decoder_init_status;

	if((decoder = FLAC__stream_decoder_new()) == NULL) {
		fprintf(stderr, "DECODER-ERROR: allocating decoder\n");
		return 1;
	}
	(void)FLAC__stream_decoder_set_md5_checking(decoder, true);

	decoder_init_status = FLAC__stream_decoder_init_file(decoder, flacFileWithJpeg,
		 write_callback, metadata_callback, error_callback, /*client_data=*/NULL);

	if(decoder_init_status != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
		fprintf(stderr, "ERROR: initializing decoder: %s\n", FLAC__StreamDecoderInitStatusString[init_status]);
		decodeOk = false;
	}

	if(decodeOk) {
		decodeOk = FLAC__stream_decoder_process_until_end_of_stream(decoder);
		fprintf(stderr, "decoding: %s\n", ok? "succeeded" : "FAILED");
		fprintf(stderr, "   state: %s\n", FLAC__StreamDecoderStateString[FLAC__stream_decoder_get_state(decoder)]);
	}

	FLAC__stream_decoder_delete(decoder);
	printf("\n[***] DECODE PHASE ENDED -> SUCCESS!\n");


	return 0;
}

int main(int argc, char *argv[])
{
	fuzzme(argv[1]);
	return 0;
}
