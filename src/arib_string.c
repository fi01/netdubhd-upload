#ifdef _WIN32
#define USE_WINAPI 1
#else
#define USE_WINAPI 0
#define _FILE_OFFSET_BITS	64 
#endif

#if USE_WINAPI
#include <windows.h>
#else
#include <iconv.h>
#include <errno.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "arib_string.h"
#include "arib_char.h"

#define CP_JIS2022		(50220)

#define MAX_CHAR_BYTES		(4)	// UTF-8

#define CONV_SP_CHAR		"\xe3\x80\x80"
#define CONV_FAIL_CHAR		"\xe2\x96\xa0"
#define CONV_UNSUPPORT_CHAR	"\xe2\x96\xa1"

#if !USE_WINAPI
static iconv_t icd = NULL;
#endif

void print_utf8_string(const unsigned char *s)
{
#if USE_WINAPI
	WCHAR *wbuf;
	unsigned char *buf;
	int wlen;
	int len;

	wlen = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
	if (wlen == 0)
	{
		fprintf(stderr, "fail: 1: MultiByteToWideChar: %ld\n", GetLastError());
		exit(1);
	}

	wbuf = malloc((wlen + 1) * sizeof wbuf[0]);
	if (!wbuf)
	{
		fprintf(stderr, "Out of memory\n");
		exit(1);
	}

	wlen = MultiByteToWideChar(CP_UTF8, 0, s, -1, wbuf, wlen);
	if (wlen == 0)
	{
		fprintf(stderr, "fail: 2: MultiByteToWideChar: %ld\n", GetLastError());
		exit(1);
	}
	wbuf[wlen] = '\0';

	if (1)
	{
		static const struct
		{
			int c1;
			int c2;
		} conv[] =
		{
			{ 0x2014, 0x2015 }, // EM DASH
			{ 0x005c, 0xff3c }, // REVERSE SOLIDUS
			{ 0xff5e, 0x301c }, // WAVE DASH
			{ 0x2225, 0x2016 }, // DOUBLE VERTICAL LINE
			{ 0xff0d, 0x2212 }, // MINUS SIGN
			{ 0xffe1, 0x00a3 }, // POUND SIGN
			{ 0xffe0, 0x00a2 }, // CENT SIGN
			{ 0xffe2, 0x00ac }, // NOT SIGN
			{ 0x203e, 0xffe3 }, // OVERLINE
			{ -1 }
		};

		int i, j;

		for (i = 0; wbuf[i]; i++)
			for (j = 0; conv[j].c1 >= 0; j++)
				if (wbuf[i] == conv[j].c2)
				{
					wbuf[i] = conv[j].c1;
					break;
				}
	}

	len = WideCharToMultiByte(CP_THREAD_ACP, 0, wbuf, wlen, NULL, 0, NULL, NULL);
	if (len == 0)
	{
		fprintf(stderr, "fail: 1: WideCharToMultiByte: %ld\n", GetLastError());
		exit(1);
	}

	buf = malloc(len + 1);
	if (!buf)
	{
		fprintf(stderr, "Out of memory\n");
		exit(1);
	}

	len = WideCharToMultiByte(CP_THREAD_ACP, 0, wbuf, wlen, buf, len, NULL, NULL);
	if (len == 0)
	{
		fprintf(stderr, "fail: 2: WideCharToMultiByte: %ld\n", GetLastError());
		exit(1);
	}
	buf[len] = '\0';

	printf("%s", buf);

	free(wbuf);
	free(buf);
#else
	printf("%s", s);
#endif
}


enum
{
	PLANE_UNKNOWN,
	PLANE_KANJI,
	PLANE_ALPHANUMERIC,
	PLANE_HIRAGANA,
	PLANE_KATAKANA,
	PLANE_MOSAIC_A,
	PLANE_MOSAIC_B,
	PLANE_MOSAIC_C,
	PLANE_MOSAIC_D,
	PLANE_PROP_ALPHANUMERIC,
	PLANE_PROP_HIRAGANA,
	PLANE_PROP_KATAKANA,
	PLANE_JIS_X0201_KATAKANA,
	PLANE_JIS_KANJI_PLANE_1,
	PLANE_JIS_KANJI_PLANE_2,
	PLANE_ADDITIONAL_SYMBOLS,
	PLANE_DRCS0,
	PLANE_DRCS1,
	PLANE_DRCS2,
	PLANE_DRCS3,
	PLANE_DRCS4,
	PLANE_DRCS5,
	PLANE_DRCS6,
	PLANE_DRCS7,
	PLANE_DRCS8,
	PLANE_DRCS9,
	PLANE_DRCS10,
	PLANE_DRCS11,
	PLANE_DRCS12,
	PLANE_DRCS13,
	PLANE_DRCS14,
	PLANE_DRCS15,
	PLANE_MACRO
};

enum
{
	BML_NUL = 0x00,
	BML_BEL = 0x07,
	BML_APB,
	BML_APF,
	BML_APD,
	BML_APU,
	BML_CS,
	BML_APR,
	BML_LS1,
	BML_LS0,
	BML_PAPF = 0x16,
	BML_CAN = 0x018,
	BML_SS2,
	BML_ESC = 0x1b,
	BML_APS,
	BML_SS3,
	BML_RS,
	BML_US,
	BML_SP,
	BML_DEL = 0x7f,
	BML_BKF,
	BML_RDF,
	BML_GRF,
	BML_YLF,
	BML_BLF,
	BML_MGF,
	BML_CNF,
	BML_WHF,
	BML_SSZ,
	BML_MSZ,
	BML_NSZ,
	BML_SZX,
	BML_COL = 0x90,
	BML_FLC,
	BML_CDC,
	BML_POL,
	BML_WMM,
	BML_MACRO,
	BML_HLC = 0x97,
	BML_RPC,
	BML_SPL,
	BML_STL,
	BML_CSI,
	BML_TIME = 0x9d,
	BML_10_0 = 0xa0,
	BML_15_15 = 0xff
};

static int plane_g[4];


static unsigned char next_char(unsigned char const **s, int *n)
{
	(*n)--;
	return *(*s)++;
}

static int is_double_char(int codeset, unsigned char c)
{
	if (c == BML_SP)
		return 0;

	switch (codeset)
	{
	case PLANE_KANJI:
	case PLANE_JIS_KANJI_PLANE_1:
	case PLANE_JIS_KANJI_PLANE_2:
	case PLANE_ADDITIONAL_SYMBOLS:
	case PLANE_DRCS0:
		return 1;
	}

	return 0;
}

static void set_codeset(int idx, unsigned char c, int is_double)
{
	switch (c)
	{
	case 0x42:
		plane_g[idx] = PLANE_KANJI;
		break;

	case 0x4a:
		plane_g[idx] = PLANE_ALPHANUMERIC;
		break;

	case 0x30:
		plane_g[idx] = PLANE_HIRAGANA;
		break;

	case 0x31:
		plane_g[idx] = PLANE_KATAKANA;
		break;

	case 0x32:
		plane_g[idx] = PLANE_MOSAIC_A;
		break;

	case 0x33:
		plane_g[idx] = PLANE_MOSAIC_B;
		break;

	case 0x34:
		plane_g[idx] = PLANE_MOSAIC_C;
		break;

	case 0x35:
		plane_g[idx] = PLANE_MOSAIC_D;
		break;

	case 0x36:
		plane_g[idx] = PLANE_PROP_ALPHANUMERIC;
		break;

	case 0x37:
		plane_g[idx] = PLANE_PROP_HIRAGANA;
		break;

	case 0x38:
		plane_g[idx] = PLANE_PROP_KATAKANA;
		break;

	case 0x49:
		plane_g[idx] = PLANE_JIS_X0201_KATAKANA;
		break;

	case 0x39:
		plane_g[idx] = PLANE_JIS_KANJI_PLANE_1;
		break;

	case 0x3a:
		plane_g[idx] = PLANE_JIS_KANJI_PLANE_2;
		break;

	case 0x3b:
		plane_g[idx] = PLANE_ADDITIONAL_SYMBOLS;
		break;

	default:
		plane_g[idx] = PLANE_UNKNOWN;
		fprintf(stderr, "code set: unknown: 0x%02x\n", c);
		exit(1);
	}

	if (is_double_char(plane_g[idx], 0x21) != is_double)
	{
		fprintf(stderr, "DRCS code set: wrong size: 0x%02x\n", c);
		exit(1);
	}
}

static void set_codeset_drcs(int idx, unsigned char c, int is_double)
{
	if (c < 0x40 || c > 0x4f)
	{
		plane_g[idx] = PLANE_UNKNOWN;
		fprintf(stderr, "DRCS code set: unknown: 0x%02x\n", c);
		exit(1);
	}

	plane_g[idx] = PLANE_DRCS0 + c - 0x40;

	if (is_double_char(plane_g[idx], 0x21) != is_double)
	{
		fprintf(stderr, "DRCS code set: wrong size: 0x%02x\n", c);
		exit(1);
	}
}

static void conver_single_char(int codeset, unsigned char c, unsigned char buf[])
{
	int c_end = BML_DEL - 1;

	if (codeset == PLANE_JIS_X0201_KATAKANA)
		c_end = BML_DEL;

 	c &= 0x7f;

	if (c == BML_SP)
	{
		strcpy(buf, CONV_SP_CHAR);
		return;
	}

	if (c < BML_SP || c > c_end)
	{
		strcpy(buf, CONV_FAIL_CHAR);
		return;
	}

	switch (codeset)
	{
	case PLANE_HIRAGANA:
	case PLANE_PROP_HIRAGANA:
		strcpy(buf, arib_hiragana_table[c - BML_SP - 1]);
		return;

	case PLANE_KATAKANA:
	case PLANE_PROP_KATAKANA:
		strcpy(buf, arib_katakana_table[c - BML_SP - 1]);
		return;

	case PLANE_ALPHANUMERIC:
	case PLANE_PROP_ALPHANUMERIC:
		strcpy(buf, arib_alpahbet_table[c - BML_SP - 1]);
		return;

	case PLANE_JIS_X0201_KATAKANA:
		strcpy(buf, arib_0201kana_table[c - BML_SP - 1]);
		return;
	}

	fprintf(stderr, "codeset(0x%x) is not supported\n", codeset);
	strcpy(buf, CONV_UNSUPPORT_CHAR);
}

static void convert_double_char(int codeset, unsigned char b, unsigned char b2, unsigned char buf[])
{
	unsigned char jisBuf[] = { 0x1b, 0x24, 0x42, b, b2, 0x00 };
#if USE_WINAPI
	WCHAR wbuf[2];
#else
	char *inp, *outp;
	size_t insz, outsz;
#endif

 	b &= 0x7f;
 	b2 &= 0x7f;

	switch (codeset)
	{
	case PLANE_KANJI:
	case PLANE_JIS_KANJI_PLANE_1:
#if USE_WINAPI
		if (MultiByteToWideChar(CP_JIS2022, 0, jisBuf, -1, wbuf, 2) == 0)
		{
			fprintf(stderr, "conversion fail: JIS(%d, %d): MultiByteToWideChar: %ld\n", b, b2, GetLastError());
			strcpy(buf, "■");
			return;
		}
		wbuf[1] = '\0';

		if (WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, buf, MAX_CHAR_BYTES + 1, NULL, NULL) == 0)
		{
			fprintf(stderr, "conversion fail: JIS(%d, %d): WideCharToMultiByte: %ld\n", b, b2, GetLastError());
			strcpy(buf, "■");
			return;
		}
#else
		inp = jisBuf;
		insz = strlen(inp);
		outp = buf;
		outsz = MAX_CHAR_BYTES;
		if (iconv(icd, &inp, &insz, &outp, &outsz) == -1)
		{
			fprintf(stderr, "iconv fail: %d\n", errno);
			exit(1);
		};

		*outp = '\0';
#endif

		return;

	case PLANE_ADDITIONAL_SYMBOLS:
		//fprintf(stderr, "additional symbol: JIS(%d,%d)\n", b, b2);
		strcpy(buf, "■");
		return;
	}

	fprintf(stderr, "codeset(0x%x) is not supported\n", codeset);
	strcpy(buf, "□");
}

const char *get_arib_string(const unsigned char *s, int len)
{
	int *plane_gl;
	int *plane_gr;
	int *plane_ss;

	char *buf;
	int bufsize;
	int buflen;

#if !USE_WINAPI
	icd = iconv_open("UTF-8", "ISO-2022-JP");
#endif

	plane_g[0] = PLANE_KANJI;
	plane_g[1] = PLANE_ALPHANUMERIC;
	plane_g[2] = PLANE_HIRAGANA;
	plane_g[3] = PLANE_KATAKANA;

	plane_gl = &plane_g[0];
	plane_gr = &plane_g[2];
	plane_ss = NULL;

	bufsize = 32;
	buf = malloc(bufsize);
	if (!buf)
	{
		fprintf(stderr, "get_arib_string: out of memory\n");
		return NULL;
	}

	buflen = 0;
	buf[0] = '\0';

	while (len > 0)
	{
		int c = next_char(&s, &len);
		int idx;

		if (buflen + 1 > bufsize)
		{
			fprintf(stderr, "get_arib_string: buffer over flow\n");
			exit(1);
		}

		if (buflen + MAX_CHAR_BYTES + 1 > bufsize)
		{
			unsigned char *p;

			bufsize += 32;
			p = realloc(buf, bufsize);
			if (!p)
			{
				fprintf(stderr, "get_arib_string: out of memory: %s for %d\n", buf, bufsize);
				free(buf);
				return NULL;
			}

			buf = p;
		}

		if (c >= BML_SP && c < BML_DEL)
		{
			if (!is_double_char(*plane_gl, c))
				conver_single_char(*plane_gl, c, &buf[buflen]);
			else
			{
				if (len <= 0)
					goto error_eos;

				convert_double_char(*plane_gl, c, next_char(&s, &len), &buf[buflen]);
			}

			if (plane_ss)
			{
				plane_gl = plane_ss;
				plane_ss = NULL;
			}

			buflen += strlen(&buf[buflen]);
			continue;
		}

		if (c >= BML_10_0 && c <= BML_15_15)
		{
			if (!is_double_char(*plane_gr, c))
				conver_single_char(*plane_gr, c, &buf[buflen]);
			else
			{
				if (len <= 0)
					goto error_eos;

				convert_double_char(*plane_gr, c, next_char(&s, &len), &buf[buflen]);
			}

			buflen += strlen(&buf[buflen]);
			continue;
		}

		switch (c)
		{
		case BML_ESC:
			if (len <= 0)
				goto error_eos;

			c = next_char(&s, &len);

			if (c >= 0x28 && c<= 0x2b)
			{
				idx = c - 0x28;

				if (len <= 0)
					goto error_eos;

				c = next_char(&s, &len);

				if (c == 0x20)
				{
					if (len <= 0)
						goto error_eos;

					c = next_char(&s, &len);
					set_codeset_drcs(idx, c, 0);
				}
				else
					set_codeset(idx, c, 0);

				break;
			}

			if (c == 0x24)
			{
				if (len <= 0)
					goto error_eos;

				c = next_char(&s, &len);

				if (c >= 0x28 && c <= 0x2b)
				{
					idx = c - 0x28;

					if (len <= 0)
						goto error_eos;

					c = next_char(&s, &len);

					if (c == 0x20)
					{
						if (len <= 0)
							goto error_eos;

						c = next_char(&s, &len);
						set_codeset_drcs(idx, c, 1);
					}
					else
						set_codeset(idx, c, 1);
				}
				else
					set_codeset(0, c, 1);

				break;
			}

			switch (c)
			{
			// LS2
			case 0x6e:
				plane_gl = &plane_g[2];
				break;
			// LS3
			case 0x6f:
				plane_gl = &plane_g[3];
				break;
			// LS1R
			case 0x7e:
				plane_gr = &plane_g[1];
				break;
			// LS2R
			case 0x7d:
				plane_gr = &plane_g[2];
				break;
			// LS3R
			case 0x7c:
				plane_gr = &plane_g[3];
				break;
			}
			break;

		case BML_LS0:
			plane_gl = &plane_g[0];
			break;

		case BML_LS1:
			plane_gl = &plane_g[1];
			break;

		case BML_SS2:
			if (!plane_ss)
				plane_ss = plane_gl;

			plane_gl = &plane_g[2];
			break;

		case BML_SS3:
			if (!plane_ss)
				plane_ss = plane_gl;

			plane_gl = &plane_g[3];
			break;
		}
	}

#if !USE_WINAPI
	iconv_close(icd);
	icd = NULL;
#endif

	return buf;

error_eos:
	fprintf(stderr, "Unexpected end of string\n");
	free(buf);

#if !USE_WINAPI
	iconv_close(icd);
	icd = NULL;
#endif

	return NULL;
}

void print_arib_string(const unsigned char *s, int len)
{
	const char *p = get_arib_string(s, len);

	if (p)
	{
		printf("%s", p);
		free((void *)p);
	}
	else
		printf("ERROR!!");
}

