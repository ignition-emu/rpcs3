/* Checks the FFmpeg the Ignition embed links: every component listed in
   required.txt is present, the build is LGPL, and its configuration enables
   nothing GPL, version 3 or non-free. Prints the licence and configure line.
   Usage: check_ffmpeg required.txt */
#include <stdio.h>
#include <string.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>

static int missing = 0;

static void need(const char* kind, const char* name)
{
	int ok = 0;
	if (!strcmp(kind, "decoder")) ok = avcodec_find_decoder_by_name(name) != NULL;
	else if (!strcmp(kind, "encoder")) ok = avcodec_find_encoder_by_name(name) != NULL;
	else if (!strcmp(kind, "demuxer")) ok = av_find_input_format(name) != NULL;
	else if (!strcmp(kind, "muxer")) ok = av_guess_format(name, NULL, NULL) != NULL;
	if (!ok)
	{
		printf("MISSING %s %s\n", kind, name);
		missing++;
	}
}

int main(int argc, char** argv)
{
	FILE* list = argc > 1 ? fopen(argv[1], "r") : NULL;
	char line[1024];
	if (!list)
	{
		printf("usage: check_ffmpeg required.txt\n");
		return 2;
	}
	while (fgets(line, sizeof line, list))
	{
		char* kind = strtok(line, " \t\r\n");
		if (!kind || kind[0] == '#') continue;
		for (char* name = strtok(NULL, " \t\r\n"); name; name = strtok(NULL, " \t\r\n"))
			need(kind, name);
	}
	fclose(list);

	const char* licence = avutil_license();
	const char* config = avutil_configuration();
	printf("license: %s\nconfiguration: %s\n", licence, config);
	if (!strstr(licence, "LGPL") || strstr(config, "--enable-gpl") || strstr(config, "--enable-version3") || strstr(config, "--enable-nonfree"))
	{
		printf("NOT LGPL\n");
		return 1;
	}
	printf(missing ? "%d components missing\n" : "all required components present\n", missing);
	return missing ? 1 : 0;
}
