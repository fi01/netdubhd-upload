// UTF-8 text

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "arib_string.h"
#include "arib_genre.h"

#define CREATEREQ_EXT	".req"

#define TS_PID_SIT	(0x001f)
#define TS_SIT_ID	(0x7f)


typedef struct
{
	int service_id;
	int channel;
} ts_info_channel;

typedef struct
{
	const unsigned char *dc_title;
	const unsigned char *dc_date;
	const unsigned char *arib_objectType;
	const unsigned char *upnp_genre;
	int upnp_channelNr;
	const unsigned char *upnp_longDescription;

	const unsigned char *_longDescription;
	ts_info_channel *_ts_info;
	int _num_ts_info;
} dlna_elements;

unsigned int calc_crc32(const unsigned char *buf, int len);

static int read188(FILE *fp, unsigned char *buf)
{
	return fread(buf, 188, 1, fp) != 1;
}

static int read188from192(FILE *fp, unsigned char *buf)
{
	if (fread(buf, 4, 1, fp) != 1)
		return 1;

	return read188(fp, buf);
}

void dlna_elements_init(dlna_elements *e)
{
	memset(e, 0, sizeof *e);
	e->upnp_channelNr = -1;
}

void free_if_allocated(unsigned char const **p)
{
	if (*p == NULL)
		return;

	free((void *)*p);
	*p = NULL;
}

void dlna_elements_free(dlna_elements *e)
{
	free_if_allocated(&e->dc_title);
	free_if_allocated(&e->dc_date);
	free_if_allocated(&e->arib_objectType);
	free_if_allocated(&e->upnp_genre);
	free_if_allocated(&e->upnp_longDescription);
	free_if_allocated(&e->_longDescription);
	free_if_allocated((unsigned char const **)&e->_ts_info);
}

void dump_dlna_elements(const dlna_elements *e)
{
	if (e->dc_title)
		printf("dc_title\t%s\n", e->dc_title);

	if (e->dc_date)
		printf("dc_date\t%s\n", e->dc_date);

	if (e->arib_objectType)
		printf("arib_objectType\t%s\n", e->arib_objectType);

	if (e->upnp_genre)
		printf("upnp_genre\t%s\n", e->upnp_genre);

	if (e->upnp_channelNr >= 0)
		printf("upnp_channelNr\t%d\n", e->upnp_channelNr);

	if (e->upnp_longDescription)
		printf("upnp_longDescription\t%s\n", e->upnp_longDescription);
}

static void print_dlna_str(FILE *fp, const unsigned char *tag, const unsigned char *val)
{
	if (val)
		fprintf(fp, "&lt;%s&gt;%s&lt;/%s&gt;\n", tag, val, tag);
}

static void print_dlna_int(FILE *fp, const unsigned char *tag, int i)
{
	if (i >= 0)
		fprintf(fp, "&lt;%s&gt;%d&lt;/%s&gt;\n", tag, i, tag);
}

void dump_dlna_createreq(FILE *fp, const dlna_elements *e)
{
	fprintf(fp, "%s",
		"<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"\r\n"
		"s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n"
		"<s:Body><u:CreateObject xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\">\n"
		"<ContainerID>DLNA.ORG_AnyContainer</ContainerID>\n"
		"<Elements>&lt;DIDL-Lite xmlns=&quot;urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/&quot; xmlns:dc=&quot;http://purl.org/dc/elements/1.1/&quot; xmlns:upnp=&quot;urn:schemas-upnp-org:metadata-1-0/upnp/&quot; xmlns:arib=&quot;urn:schemas-arib-or-jp:elements-1-0/&quot;&gt;\n"
		"&lt;item id=&quot;&quot; parentID=&quot;DLNA.ORG_AnyContainer&quot; restricted=&quot;0&quot;&gt;\n");

	print_dlna_str(fp, "dc:title", e->dc_title);

	fprintf(fp, "%s",
		"&lt;upnp:class&gt;object.item.videoItem&lt;/upnp:class&gt;\n");


	print_dlna_str(fp, "dc:date", e->dc_date);
	print_dlna_str(fp, "arib:objectType", e->arib_objectType);
	print_dlna_str(fp, "upnp:genre", e->upnp_genre);
	print_dlna_int(fp, "upnp:channelNr", e->upnp_channelNr);
	print_dlna_str(fp, "upnp:longDescription", e->upnp_longDescription);

	fprintf(fp, "%s",
		"&lt;res protocolInfo=&quot;*:*:video/vnd.dlna.mpeg-tts:ARIB.OR.JP_PN=MPEG_TTS_CP&quot;&gt;&lt;/res&gt;\n"
		"&lt;/item&gt;\n"
		"&lt;/DIDL-Lite&gt;\n"
		"</Elements>\n"
		"</u:CreateObject>\n"
		"</s:Body>\r\n"
		"</s:Envelope>");
}

static void str_add(unsigned char const **s, const unsigned char *add, int len)
{
	const unsigned char *ss;
	unsigned char *p;

	ss = *s;
	if (ss)
	{
		unsigned char *pp;

		p = realloc((void *)ss, strlen(ss) + len + 1);
		if (p == NULL)
		{
			fprintf(stderr, "out of memory\n");
			exit(1);
		}

		pp = &p[strlen(p)];
		memcpy(pp, add, len);
		pp[len] = '\0';

		*s = p;
		return;
	}

	p = malloc(len + 1);
	if (p == NULL)
	{
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	memcpy(p, add, len);
	p[len] = '\0';
	*s = p;
}

static void add_longDescription(dlna_elements *e)
{
	const unsigned char *s;

	if (!e->_longDescription)
		return;

	s = get_arib_string(e->_longDescription, strlen(e->_longDescription));
	str_add(&e->upnp_longDescription, s, strlen(s));
	free((void *)s);
	free_if_allocated(&e->_longDescription);
}

void parse_sit(dlna_elements *e, const unsigned char *p)
{
	const unsigned char *s;
	int len;
	int n;
	int i;

	switch (p[0])
	{
	case 0xc2:
		// network_id
		n = p[7] * 256 + p[8];

		switch (n)
		{
		case 0x0004:
			s = "ARIB_BS";
			break;

		case 0x0006:	// E2 CS1
		case 0x0007:	// E2 CS2
		case 0x000a:	// CS HD
			s = "ARIB_CS";
			break;

		default:
			s = "ARIB_TB";
		}

		free_if_allocated(&e->arib_objectType);
		e->arib_objectType = strdup(s);

		break;

	case 0xc3:
		n = 3;
		//if (p[14] & 0x01)
		//	n = 15;

		{
			double mdj = p[n] * 256 + p[n + 1];
			int _y = (mdj - 15078.2) / 365.25;
			int _m = (mdj - 14956.1 - (int)(_y * 365.25)) / 30.6001;
			int d = mdj - 14956 - (int)(_y * 365.25) - (int)(_m * 30.6001);
			int k = (_m == 14 || _m == 15);
			int y = _y + k + 1900;
			int m = _m - 1 - k * 12;
			unsigned char datebuf[32];

			sprintf(datebuf, "%04d-%02d-%02dT%02x:%02x:%02x", y, m, d, p[n + 2], p[n + 3], p[n + 4]);

			free_if_allocated(&e->dc_date);
			e->dc_date = strdup(datebuf);
		}

		break;

	case 0xcd:
		n = (p[3] & 0xfc) >> 2;

		i = 4 + n;
		n = (p[3] & 3) + i;

		while (i < n)
		{
			int j;

			free_if_allocated((unsigned char const **)&e->_ts_info);
			e->_num_ts_info = p[i + 1];
			e->_ts_info = malloc(e->_num_ts_info * sizeof e->_ts_info[0]);

			for (j = 0; j < p[i + 1]; j++)
			{
				e->_ts_info[j].service_id = p[i + 2 + j * 2] * 256 + p[i + 2 + j * 2 + 1];
				e->_ts_info[j].channel = p[2] * 10 + j + 1;
			}

			i += 2 + p[i + 1] * 2;
		}

		break;

	case 0x4d:
		n = p[5];

		free_if_allocated(&e->dc_title);
		e->dc_title = get_arib_string(&p[6], n);

		free_if_allocated(&e->upnp_longDescription);
		free_if_allocated(&e->_longDescription);

		e->upnp_longDescription = get_arib_string(&p[n + 7], p[6 + n]);

		break;

	case 0x4e:
		n = p[6];

		i = 0;
		while (i < n)
		{
			len = p[7 + i];
			if (len)
			{
				add_longDescription(e);

				s = get_arib_string(&p[8 + i], len);
				str_add(&e->upnp_longDescription, "\n", 1);
				str_add(&e->upnp_longDescription, s, strlen(s));
				str_add(&e->upnp_longDescription, "\n", 1);
				free((void *)s);
			}

			i += len + 1;
			len = p[7 + i];

			if (len)
				str_add(&e->_longDescription, &p[8 + i], len);

			i += len + 1;
		}

		len = p[7 + i];
		if (len)
		{
			add_longDescription(e);

			s = get_arib_string(&p[8 + i], len);
			str_add(&e->_longDescription, "\n", 1);
			str_add(&e->upnp_longDescription, s, strlen(s));
			str_add(&e->_longDescription, "\n", 1);
		}

		break;

	case 0x54:
		len = p[1] + 2;

		for (i = 2; i < len; i += 2)
		{
			free_if_allocated(&e->upnp_genre);
			e->upnp_genre = strdup(arib_genre_detail_info[p[i] >> 4][p[i] & 0xf]);
		}

		break;
	}
}

int get_ts_pid(const unsigned char *buf)
{
	return (buf[1] & 0x1f) * 256 + buf[2];
}

int is_ts_psi_top(const unsigned char *buf)
{
	return (buf[1] & 0x40) != 0;
}

int get_ts_adaptation_filed_len(const unsigned char *buf)
{
	if (buf[3] & 0x20)
		return buf[4] + 1;

	return 1;
}

int get_ts_section_len(const unsigned char *buf)
{
	return (buf[1] & 0x0f) * 256 + buf[2];
}

int main(int argc, const char *argv[])
{
	FILE *fp;
	int pos;
	int started;
	int buflen;
	int adaplen;
	int seclen;
	int bufneed;
	char save[4];
	const unsigned char *skip_title;
	const unsigned char *fname_m2ts;
	unsigned char *fname_createreq;

	if (argc != 2)
		return 1;
	fname_m2ts = argv[1];

	fp = fopen(fname_m2ts, "rb");
	if (!fp)
		return 2;

	{
		int i;

		pos = -1;
		for (i = 0; fname_m2ts[i]; i++)
			if (fname_m2ts[i] == '.')
				pos = i;

		if (pos < 0)
			pos = i;

		fname_createreq = malloc(pos + strlen(CREATEREQ_EXT) + 1);
		if (!fname_createreq)
			return 3;

		strncpy(fname_createreq, fname_m2ts, pos);
		strcpy(fname_createreq + pos, CREATEREQ_EXT);
	}

	started = 0;
	buflen = 0;

	skip_title = NULL;

	for (pos = 0; !feof(fp); pos++)
	{
		unsigned char buf[188 * 16];
		unsigned char *p;
		const unsigned char *sit;

		//printf("\r%d ", pos);

		p = &buf[buflen];
		if (read188from192(fp, p))
			break;

		if (get_ts_pid(p) != TS_PID_SIT)
			continue;

		if (!is_ts_psi_top(p))
		{
			if (started)
			{
				memcpy(p, save, 4);
				buflen += 188;

				if (buflen < bufneed)
				{
					buflen -= 4;
					memcpy(save, &buf[buflen], 4);
					continue;
				}
			}
			else
			{
				// not started, skip it
				continue;
			}
		}

		if (!started)
		{
			started = 1;

			printf("Start SIT at: %d\n", pos);

			adaplen = get_ts_adaptation_filed_len(buf);
			sit = &buf[4] + adaplen;
			buflen = 188;

			//printf("adaplen = %d\n", adaplen);

			if (sit[0] != TS_SIT_ID)
			{
				fprintf(stderr, "SIT identifier is wrong: %02x %02x\n", sit[0], sit[1]);
				started = 0;
				buflen = 0;
				continue;
			}

			seclen = get_ts_section_len(sit);
			bufneed = adaplen + seclen + 3;

			//printf("Section length: %d\n", seclen);
			//printf("buffer need: %d\n", bufneed);
			//printf("buflen = %d\n", buflen);

			if (buflen < bufneed)
			{
				buflen -= 4;
				memcpy(save, &buf[buflen], 4);
				continue;
			}
		}

		printf("\nready to process\n\n");

		{
			dlna_elements e;
			unsigned long crc32;
			int n;
			int off;

			sit = &buf[4] + adaplen;
			n = (sit[8] & 0x0f) * 256 + sit[9];

			crc32 = calc_crc32(sit, seclen + 3 - 4);
			//printf("calc CRC32: 0x%08lx\n", crc32);
			//printf("CRC32: 0x%02x%02x%02x%02x\n",
			//	sit[seclen + 3 - 4], sit[seclen + 3 - 3], sit[seclen + 3 - 2], sit[seclen + 3 - 1]);

			if (((crc32 & 0xff000000) >> 24) != sit[seclen + 3 - 4]
			 || ((crc32 & 0x00ff0000) >> 16) != sit[seclen + 3 - 3]
			 || ((crc32 & 0x0000ff00) >> 8) != sit[seclen + 3 - 2]
			 || (crc32 & 0x000000ff) != sit[seclen + 3 - 1])
			{
				fprintf(stderr, "CRC is wrong. ignore this SIT\n");
				started = 0;
				buflen = 0;
				continue;
			}

			dlna_elements_init(&e);

			for (off = 10; off < 10 + n; off += sit[off + 1] + 2)
				parse_sit(&e, &sit[off]);

			while (off < seclen + 3 - 4)
			{
				int service_id = sit[off] * 256 + sit[off + 1];
				int nn;

				//printf("service_id: %d\n", service_id);

				if (e.arib_objectType && strcmp(e.arib_objectType, "ARIB_TB") != 0)
					e.upnp_channelNr = service_id * 10;
				else
					for (nn = 0; nn < e._num_ts_info; nn++)
						if (e._ts_info[nn].service_id == service_id)
						{
							e.upnp_channelNr = e._ts_info[nn].channel * 10;
							break;
						}

				n = (sit[off + 2] & 0x0f) * 256 + sit[off + 3];
				//printf(" service_loop_length: 0x%x\n", n);

				off += 4;
				for (nn = off + n; off < nn; off += sit[off + 1] + 2)
					parse_sit(&e, &sit[off]);
			}

			if (off != seclen + 3 - 4)
			{
				fprintf(stderr, "Error: Table size is wrong!!\n");
				break;
			}

			printf("Table end at 0x%x\n\n", off + 4);

			add_longDescription(&e);

			if (e.dc_title)
			{
				if (!skip_title || strcmp(skip_title, e.dc_title) != 0)
				{
					FILE *out;
					unsigned char linebuf[256];

					out = fopen(fname_createreq, "wb");
					if (!out)
					{
						fprintf(stderr, "failed to creat file: %s\n", fname_createreq);
						break;
					}

					dump_dlna_createreq(out, &e);
					fclose(out);

					print_utf8_string(e.dc_title);
					printf("\n");

					if (e.upnp_channelNr >= 0)
						printf("Ch. %03d\n", e.upnp_channelNr);

					if (e.upnp_longDescription)
					{
						printf("\n");
						print_utf8_string(e.upnp_longDescription);
						printf("\n");
					}

					print_utf8_string("\nこのタイトル情報でよろしいですか(Y/N)？\n");

					if (fgets(linebuf, sizeof linebuf, stdin) == NULL)
						break;
					if (*linebuf != 'n' && *linebuf != 'N')
						break;

					free_if_allocated(&skip_title);
					skip_title = strdup(e.dc_title);
				}
			}

			dlna_elements_free(&e);
		}

		started = 0;
		buflen = 0;
	}

	fclose(fp);

	return 0;
}

unsigned int calc_crc32(const unsigned char *buf, int len)
{
	static const unsigned int crc_table[] =
	{
		0x00000000, 0x04C11DB7, 0x09823B6E, 0x0D4326D9, 0x130476DC, 0x17C56B6B, 0x1A864DB2, 0x1E475005,
		0x2608EDB8, 0x22C9F00F, 0x2F8AD6D6, 0x2B4BCB61, 0x350C9B64, 0x31CD86D3, 0x3C8EA00A, 0x384FBDBD,
		0x4C11DB70, 0x48D0C6C7, 0x4593E01E, 0x4152FDA9, 0x5F15ADAC, 0x5BD4B01B, 0x569796C2, 0x52568B75,
		0x6A1936C8, 0x6ED82B7F, 0x639B0DA6, 0x675A1011, 0x791D4014, 0x7DDC5DA3, 0x709F7B7A, 0x745E66CD,
		0x9823B6E0, 0x9CE2AB57, 0x91A18D8E, 0x95609039, 0x8B27C03C, 0x8FE6DD8B, 0x82A5FB52, 0x8664E6E5,
		0xBE2B5B58, 0xBAEA46EF, 0xB7A96036, 0xB3687D81, 0xAD2F2D84, 0xA9EE3033, 0xA4AD16EA, 0xA06C0B5D,
		0xD4326D90, 0xD0F37027, 0xDDB056FE, 0xD9714B49, 0xC7361B4C, 0xC3F706FB, 0xCEB42022, 0xCA753D95,
		0xF23A8028, 0xF6FB9D9F, 0xFBB8BB46, 0xFF79A6F1, 0xE13EF6F4, 0xE5FFEB43, 0xE8BCCD9A, 0xEC7DD02D,
		0x34867077, 0x30476DC0, 0x3D044B19, 0x39C556AE, 0x278206AB, 0x23431B1C, 0x2E003DC5, 0x2AC12072,
		0x128E9DCF, 0x164F8078, 0x1B0CA6A1, 0x1FCDBB16, 0x018AEB13, 0x054BF6A4, 0x0808D07D, 0x0CC9CDCA,
		0x7897AB07, 0x7C56B6B0, 0x71159069, 0x75D48DDE, 0x6B93DDDB, 0x6F52C06C, 0x6211E6B5, 0x66D0FB02,
		0x5E9F46BF, 0x5A5E5B08, 0x571D7DD1, 0x53DC6066, 0x4D9B3063, 0x495A2DD4, 0x44190B0D, 0x40D816BA,
		0xACA5C697, 0xA864DB20, 0xA527FDF9, 0xA1E6E04E, 0xBFA1B04B, 0xBB60ADFC, 0xB6238B25, 0xB2E29692,
		0x8AAD2B2F, 0x8E6C3698, 0x832F1041, 0x87EE0DF6, 0x99A95DF3, 0x9D684044, 0x902B669D, 0x94EA7B2A,
		0xE0B41DE7, 0xE4750050, 0xE9362689, 0xEDF73B3E, 0xF3B06B3B, 0xF771768C, 0xFA325055, 0xFEF34DE2,
		0xC6BCF05F, 0xC27DEDE8, 0xCF3ECB31, 0xCBFFD686, 0xD5B88683, 0xD1799B34, 0xDC3ABDED, 0xD8FBA05A,
		0x690CE0EE, 0x6DCDFD59, 0x608EDB80, 0x644FC637, 0x7A089632, 0x7EC98B85, 0x738AAD5C, 0x774BB0EB,
		0x4F040D56, 0x4BC510E1, 0x46863638, 0x42472B8F, 0x5C007B8A, 0x58C1663D, 0x558240E4, 0x51435D53,
		0x251D3B9E, 0x21DC2629, 0x2C9F00F0, 0x285E1D47, 0x36194D42, 0x32D850F5, 0x3F9B762C, 0x3B5A6B9B,
		0x0315D626, 0x07D4CB91, 0x0A97ED48, 0x0E56F0FF, 0x1011A0FA, 0x14D0BD4D, 0x19939B94, 0x1D528623,
		0xF12F560E, 0xF5EE4BB9, 0xF8AD6D60, 0xFC6C70D7, 0xE22B20D2, 0xE6EA3D65, 0xEBA91BBC, 0xEF68060B,
		0xD727BBB6, 0xD3E6A601, 0xDEA580D8, 0xDA649D6F, 0xC423CD6A, 0xC0E2D0DD, 0xCDA1F604, 0xC960EBB3,
		0xBD3E8D7E, 0xB9FF90C9, 0xB4BCB610, 0xB07DABA7, 0xAE3AFBA2, 0xAAFBE615, 0xA7B8C0CC, 0xA379DD7B,
		0x9B3660C6, 0x9FF77D71, 0x92B45BA8, 0x9675461F, 0x8832161A, 0x8CF30BAD, 0x81B02D74, 0x857130C3,
		0x5D8A9099, 0x594B8D2E, 0x5408ABF7, 0x50C9B640, 0x4E8EE645, 0x4A4FFBF2, 0x470CDD2B, 0x43CDC09C,
		0x7B827D21, 0x7F436096, 0x7200464F, 0x76C15BF8, 0x68860BFD, 0x6C47164A, 0x61043093, 0x65C52D24,
		0x119B4BE9, 0x155A565E, 0x18197087, 0x1CD86D30, 0x029F3D35, 0x065E2082, 0x0B1D065B, 0x0FDC1BEC,
		0x3793A651, 0x3352BBE6, 0x3E119D3F, 0x3AD08088, 0x2497D08D, 0x2056CD3A, 0x2D15EBE3, 0x29D4F654,
		0xC5A92679, 0xC1683BCE, 0xCC2B1D17, 0xC8EA00A0, 0xD6AD50A5, 0xD26C4D12, 0xDF2F6BCB, 0xDBEE767C,
		0xE3A1CBC1, 0xE760D676, 0xEA23F0AF, 0xEEE2ED18, 0xF0A5BD1D, 0xF464A0AA, 0xF9278673, 0xFDE69BC4,
		0x89B8FD09, 0x8D79E0BE, 0x803AC667, 0x84FBDBD0, 0x9ABC8BD5, 0x9E7D9662, 0x933EB0BB, 0x97FFAD0C,
		0xAFB010B1, 0xAB710D06, 0xA6322BDF, 0xA2F33668, 0xBCB4666D, 0xB8757BDA, 0xB5365D03, 0xB1F740B4
	};

	unsigned int crc = 0xFFFFFFFF;
	int i;

	for (i = 0; i < len; i++)
		crc = (crc << 8) ^ crc_table[(crc >> 24) ^ *buf++];

	return crc;
}
