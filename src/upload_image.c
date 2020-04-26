#ifdef _WIN32
#define USE_WINAPI 1
#else
#define USE_WINAPI 0
#define _FILE_OFFSET_BITS	64 
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/stat.h>

#if USE_WINAPI
#include <windows.h>
#define I64d	"%I64d"
#else /* USE_WINAPI */
#include <sys/socket.h>
#include <netdb.h>
#define I64d	"%lld"
#endif /* USE_WINAPI */

#include "ts-filter.h"

#define stricmp(x,y)	strcasecmp(x,y)
#define strnicmp(x,y,z)	strncasecmp(x,y,z)


#define IMG_EXT		".m2ts"
#define CREATEREQ_EXT	".req"

#define MPEG2TTS_PN	"MPEG_TS_JP_T"

#define SOAP_PORT	(20081)

#define CONTENT_LENGTH	"Content-Length:"
#define CRLF		"\r\n"

#define PCP_DATA_SIZE	(192 * 8192)
#define PCP_HEADER_SIZE	(14)
#define PCP_MAX_PADDING	(15)

#define MAX_CREATE_REQ	(0x800)

#define CREATE_REQ_HEADER \
	"<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"\n" \
	"s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\n" \
	"<s:Body><u:CreateObject xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\">\n" \
	"<ContainerID>DLNA.ORG_AnyContainer</ContainerID>\n" \
	"<Elements>&lt;DIDL-Lite xmlns=&quot;urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/&quot;\n" \
	"xmlns:dc=&quot;http://purl.org/dc/elements/1.1/&quot;\n" \
	"xmlns:upnp=&quot;urn:schemas-upnp-org:metadata-1-0/upnp/&quot;\n" \
	"xmlns:arib=&quot;urn:schemas-arib-or-jp:elements-1-0/&quot;&gt;\n" \
	"&lt;item id=&quot;&quot; parentID=&quot;DLNA.ORG_AnyContainer&quot;\n" \
	"restricted=&quot;0&quot;&gt;\n"

#define CREATE_REQ_TITLE \
	"&lt;dc:title&gt;%s&lt;/dc:title&gt;\n"

#define CREATE_REQ_CLASS \
	"&lt;upnp:class&gt;object.item.videoItem&lt;/upnp:class&gt;\n"

#define CREATE_REQ_DATE \
	"&lt;dc:date&gt;%s&lt;/dc:date&gt;\n"

#define CREATE_REQ_OBJTYPE \
	"&lt;arib:objectType&gt;%s&lt;/arib:objectType&gt;\n"

#define CREATE_REQ_GENRE \
	"&lt;upnp:genre&gt;%s&lt;/upnp:genre&gt;\n"

#define CREATE_REQ_CHANNEL \
	"&lt;upnp:channelNr&gt;%s&lt;/upnp:channelNr&gt;\n"

#define CREATE_REQ_LONGDESC \
	"&lt;upnp:longDescription&gt;%s&lt;/upnp:longDescription&gt;\n"

#define CREATE_REQ_PROTOCOL \
	"&lt;res protocolInfo=&quot;*:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=%s&quot;&gt;&lt;/res&gt;\n"

#define CREATE_REQ_FOOTER \
	"&lt;/item&gt;\n" \
	"&lt;/DIDL-Lite&gt;\n" \
	"</Elements>\n" \
	"</u:CreateObject>\n" \
	"</s:Body>\n" \
	"</s:Envelope>\n"


struct metadata
{
	char *title;
	char *date;
	char *objtype;
	char *genre;
	char *channel;
	char *longdesc;
};

static int prepare_socket(const char *addr, int port)
{
	struct hostent *hostent;
	struct sockaddr_in sockaddr;
	int sock;

	hostent = gethostbyname(addr);
	if (!hostent)
	{
		fprintf(stderr, "Cannot resolve %s\n", addr);
		return -1;
	}

	sockaddr.sin_family = AF_INET;
	memcpy(&sockaddr.sin_addr, hostent->h_addr, hostent->h_length);
	sockaddr.sin_port = htons(port);

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0)
	{
		fprintf(stderr, "Socket failed\n");
		return -1;
	}

	if (connect(sock, (struct sockaddr *)&sockaddr, sizeof(sockaddr)) == -1)
	{
		fprintf(stderr, "Connect failed\n");
		return -1;
	}

	return sock;
}

static char *recv_line(int sock)
{
	char buf[1024];
	int i;

	for (i = 0; i < sizeof buf; i++)
	{
		if (recv(sock, &buf[i], 1, 0) <= 0)
			break;

		if (buf[i] == '\n')
		{
			i++;
			break;
		}
	}

	if (i == 0)
		return NULL;

	buf[i] = '\0';

	return strdup(buf);
}

static char *html_escape(const char *s)
{
	char *p;
	int size;
	int i;

	if (!s)
		return NULL;

	size = 256;
	i = 0;
	p = malloc(size);
	p[0] = '\0';

	while (*s)
	{
		char buf[2];
		char *add;
		int len;

		switch (*s)
		{
		case '<':
			add = "&lt;";
			break;
		case '>':
			add = "&gt;";
			break;
		case '"':
			add = "&quot;";
			break;
		case '&':
			add = "&amp;";
			break;
		default:
			buf[0] = *s;
			buf[1] = '\0';
			add = buf;
		}

		s++;

		len = strlen(add);
		if (i + len >= size)
		{
			void *t;

			size += 256;
			t = realloc(p, size);
			if (!t)
			{
				free(p);
				return NULL;
			}

			p = t;
		}

		strcpy(&p[i], add);
		i += len;
	}

	return p;
}

static int str2digit(const char **s)
{
	const char *p = *s;
	int n = 0;

	while (*p == ' ')
		p++;

	if (*p < '0' || *p > '9')
		return -1;

	while (*p)
	{
		if (*p < '0' || *p > '9')
			break;

		n = n * 10 + *p++ - '0';
	}

	*s = p;

	return n;
}

static char *recv_http_response(int sock)
{
	char *buf;
	int size;
	int i;

	size = 256;
	buf = malloc(size);
	if (!buf)
		return NULL;

	i = 0;
	while (1)
	{
		char *p = recv_line(sock);
		int len;

		if (!p)
		{
			free(buf);
			return NULL;
		}

		len = strlen(p);
		if (i + len + 1 > size)
		{
			char *temp;

			size += 256;
			temp = realloc(buf, size);
			if (!temp)
			{
				free(p);
				free(buf);
				return NULL;
			}
			buf = temp;
		}

		strcpy(&buf[i], p);
		free(p);
 
		if (strcmp(&buf[i], CRLF) == 0)
			break;
		i += len;
	}

	return buf;
}

static int check_http_response(const char *p, int *len)
{
	static const char http[] = "HTTP/";
	int res;
	int n;

	*len = -1;

	n = strlen(http);
	if (strncmp(p, http, n) != 0)
		return -1;

	p += n;

	n = str2digit(&p);
	if (n < 0)
		return -1;

	if (*p++ != '.')
		return -1;

	n = str2digit(&p);
	if (n < 0)
		return -1;

	if (*p++ != ' ')
		return -1;

	res = str2digit(&p);
	if (res < 0)
		return -1;

	if (*p++ != ' ')
		return -1;

	while (*p)
	{
		if (*p++ != '\n')
			continue;

		if (strnicmp(p, CONTENT_LENGTH, sizeof (CONTENT_LENGTH) - 1) == 0)
		{
			const char *s = &p[sizeof (CONTENT_LENGTH) - 1];

			n = str2digit(&s);
			if (n > 0 && strncmp(s, CRLF, strlen(CRLF)) == 0)
				*len = n;
		}
	}

	return res;
}

static char *recv_body(int sock, int len)
{
	char *p;
	int i, n;

	p = malloc(len + 1);
	if (!p)
		return NULL;

	for (i = 0; i < len; i += n)
	{
		n = recv(sock, &p[i], len - i, 0);
		if (n <= 0)
		{
			free(p);
			return NULL;
		}
	}

	p[len] = '\0';
	return p;
}

static int find_str(const char *s, const char *d)
{
	int len = strlen(d);
	int i;

	for (i = 0; s[i]; i++)
		if (strncmp(&s[i], d, len) == 0)
			return i;

	return -1;
}

static int parse_args(const char **argv, const char **addr, int *port, char **basename, char **pn)
{
	const char *s;
	void *p;
	int n;

	*addr = *argv++;
	if (!*addr)
		return 1;

	if (!*argv)
		return 1;

	*port = SOAP_PORT;
	if (**argv >= '0' && **argv <= '9')
	{
		s = *argv;
		*port = str2digit(&s);
		if (*s || *port <= 0 || *port > 65535)
			*port = SOAP_PORT;
		else
			argv++;
	}

	s = *argv++;
	if (!s)
		return 1;

	*basename = strdup(s);
	if (!*basename)
		return 1;

	n = strlen(*basename) - strlen(IMG_EXT);
	if (n > 0 && stricmp(&basename[0][n], IMG_EXT) == 0)
		basename[0][n] = '\0';

	p = html_escape(*argv++);
	if (!p)
	{
		*pn = strdup(MPEG2TTS_PN);
		if (!*pn)
			return 1;
		return 0;
	}

	*pn = html_escape(p);
	free(p);

	if (!*pn)
		return 1;

	if (*argv)
		return 1;

	return 0;
}

static int is_whitespace(unsigned char c)
{
	switch (c)
	{
	case ' ':
	case '\t':
	case '\r':
	case '\n':
		return 1;
	}

	return 0;
}

static char *validate_createreq(char *p, const char *pn)
{
	static const char start_str[] = "&lt;res";
	static const char end_str[] = "&lt;/res&gt;";
	char buf[256];
	int n, i, s, e;
	int len;

	s = find_str(p, start_str);
	if (s < 0)
	{
		fprintf(stderr, "Start string is not found\n");
		goto error_exit;
	}

	i = s + strlen(start_str);
	if (!is_whitespace(p[i++]))
	{
		fprintf(stderr, "Start string is not found\n");
		goto error_exit;
	}

	n = find_str(&p[i], end_str);
	if (n < 0)
	{
		fprintf(stderr, "End string is not found\n");
		goto error_exit;
	}

	e = i + n + strlen(end_str);
	while (is_whitespace(p[e]))
		e++;

	n = snprintf(buf, sizeof buf, CREATE_REQ_PROTOCOL, pn);
	if (n < 0 || n >= sizeof buf)
	{
		fprintf(stderr, "Not enough space to replace string\n");
		goto error_exit;
	}

	len = strlen(&p[e]);
	if (s + n > e)
	{
		char *q = realloc(p, s + n + len + 1);
		if (!q)
		{
			fprintf(stderr, "Out of memory\n");
			goto error_exit;
		}

		memmove(&q[s + n], &q[e], len + 1);
		strncpy(&q[s], buf, n);

		return q;
	}

	strncpy(&p[s], buf, n);
	memmove(&p[s + n], &p[e], len + 1);

	return p;

error_exit:
	free(p);
	return NULL;
}

static char *load_createreq_file(const char *basename, const char *pn)
{
	FILE *fp = NULL;
	struct stat statbuf;
	char *fname;
	char *p = NULL;
	off_t i;

	fname = malloc(strlen(basename) + strlen(CREATEREQ_EXT) + 1);
	if (!fname)
		return NULL;

	strcpy(fname, basename);
	strcat(fname, CREATEREQ_EXT);

	fp = fopen(fname, "rb");
	free(fname);

	if (!fp)
		return NULL;

	do
	{
		if (fstat(fileno(fp), &statbuf))
			break;

		p = malloc(statbuf.st_size + 1);
		if (!p)
			break;

		i = 0;
		while (!feof(fp))
		{
			int n = fread(&p[i], 1, statbuf.st_size - i, fp);

			if (n <= 0)
				break;

			i += n;
			if (i == statbuf.st_size)
			{
				p[i] = '\0';
				p = validate_createreq(p, pn);
				fclose(fp);
				return p;
			}
		}
	} while (0);

	fclose(fp);

	if (p)
		free(p);

	return NULL;
}

static void free_string(char **p)
{
	if (*p)
		free(*p);
	*p = NULL;
}

static void free_metadata(struct metadata *x)
{
	free_string(&x->title);
	free_string(&x->date);
	free_string(&x->objtype);
	free_string(&x->genre);
	free_string(&x->channel);
	free_string(&x->longdesc);
}

static int setup_metadata(struct metadata *x, const char *basename)
{
	memset(x, 0, sizeof (*x));

	x->title = strdup("\xe5\x90\x8d\xe7\xa7\xb0"
		"\xe6\x9c\xaa\xe8\xa8\xad\xe5\xae\x9a");
	if (!x->title)
		goto error_exit;

	x->date = strdup("2010-01-01T00:00:00");
	if (!x->date)
		goto error_exit;

	x->objtype = strdup("ARIB_TB");
	if (!x->objtype)
		goto error_exit;

	x->genre = strdup("\xe4\xb8\x8d\xe6\x98\x8e");
	if (!x->genre)
		goto error_exit;

	return 0;

error_exit:
	free_metadata(x);
	return 1;
}

#define ADD_STRING(buffer,size,position,data) \
	do \
	{ \
		char *__b = (buf); \
		int __sz = (size); \
		int __pos = *(position); \
		const char *__d = (data); \
		int __n = strlen(__d); \
		if (__sz - __pos < __n + 1) \
			goto out_of_buffer; \
		strcpy(&__b[__pos], __d); \
		*(position) += __n; \
	} while (0)

#define ADD_PRINTF(buffer,size,position,format,data) \
	do \
	{ \
		char *__b = (buf); \
		int __sz = (size); \
		int __pos = *(position); \
		const char *__d = (data); \
		int __n; \
		if (!__d) \
			break; \
		__n = snprintf(&__b[__pos], __sz - __pos, (format), __d); \
		if (__n < 0 || __n >= __sz - __pos) \
			goto out_of_buffer; \
		*(position) += __n; \
	} while (0)

static char *make_create_req(const char *basename, const char *pn)
{
	char buf[MAX_CREATE_REQ];
	struct metadata x;
	int i;
	char *p;

	p = load_createreq_file(basename, pn);
	if (p)
	{
		printf("Use %s" CREATEREQ_EXT "\n", basename);
		return p;
	}

	if (setup_metadata(&x, basename))
		return NULL;

	i = 0;
	ADD_STRING(buf, sizeof buf, &i, CREATE_REQ_HEADER);
	ADD_PRINTF(buf, sizeof buf, &i, CREATE_REQ_TITLE, x.title);
	ADD_STRING(buf, sizeof buf, &i, CREATE_REQ_CLASS);
 	ADD_PRINTF(buf, sizeof buf, &i, CREATE_REQ_DATE, x.date);
 	ADD_PRINTF(buf, sizeof buf, &i, CREATE_REQ_OBJTYPE, x.objtype);
 	ADD_PRINTF(buf, sizeof buf, &i, CREATE_REQ_GENRE, x.genre);
 	ADD_PRINTF(buf, sizeof buf, &i, CREATE_REQ_PROTOCOL, pn);
	ADD_STRING(buf, sizeof buf, &i, CREATE_REQ_FOOTER);

	free_metadata(&x);
	return strdup(buf);

out_of_buffer:
	fprintf(stderr, "Out of buffer\n");

	free_metadata(&x);
	return NULL;
}

static int send_create_object(const char *addr, int port, const char *create_req)
{
	char buf[256];
	int i, n;
	int size;
	int sock;

	size = strlen(create_req);

	i = 0;

	n = snprintf(&buf[i], sizeof (buf) - i,
		"POST /upnp/control/cds1 HTTP/1.1" CRLF
		"Host: %s:%d" CRLF
		"Content-Length: %d" CRLF
		"Content-Type: text/xml; charset=\"utf-8\"" CRLF
		"Soapaction: \"urn:schemas-upnp-org:service:ContentDirectory:1#CreateObject\"" CRLF
		"User-Agent: httppost" CRLF CRLF,
		addr, port, size);
	if (n < 0 || n >= sizeof (buf) - i)
		return -1;
	i += n;

	printf("%s", buf);

	sock = prepare_socket(addr, port);
	if (sock < 0)
		return -1;

	send(sock, buf, i, 0);
	send(sock, create_req, size, 0);

	return sock;
}

static char *recv_create_object_response(int sock)
{
	int len = 0;
	char *p;
	int n;

	p = recv_http_response(sock);
	if (!p)
		return NULL;

	printf("%s", p);

	n = check_http_response(p, &len);
	free(p);

	if (n != 200)
		return NULL;

	if (len <= 0)
		return NULL;

	return recv_body(sock, len);
}

static int process_import_uri(char *s, char **addr, int *port, char **uri)
{
	static const char protoinfo[] = "&lt;res protocolInfo=";
	static const char importuri[] = " importUri=&quot;http://";
	char *p;
	int n;

	n = find_str(s, protoinfo);
	if (n < 0)
		return 1;
	s += n + strlen(protoinfo);

	n = find_str(s, "&gt;&lt;/res&gt;");
	if (n < 0)
		return 1;
	s[n] = '\0';

	n = find_str(s, importuri);
	if (n < 0)
		return 1;

	s += n + strlen(importuri);
	n = find_str(s, "&quot;");
	if (n < 0)
		return 1;
	s[n] = '\0';

	p = strdup(s);
	if (!p)
		return 1;

	*addr = NULL;
	for (s = p; *s; s++)
	{
		if (*s == ':')
		{
			*s++ = '\0';
			*addr = strdup(p);
			*port = str2digit((const char **)&s);
			*uri = strdup(s);
			break;

		}

		if (*s == '/')
		{
			*s = '\0';
			*addr = strdup(p);
			*port = 80;
			*s = '/';
			*uri = strdup(s);
			break;
		}
	}

	free(p);

	if (!*addr)
		return 1;
	if (*port < 0 || *port > 65535)
		return 1;
	if (!*uri || **uri != '/')
		return 1;

	return 0;
}

static int send_post_header(char *s, const char *pn)
{
	char buf[MAX_CREATE_REQ];
	char *addr;
	int port;
	char *uri;
	int i, n;
	int sock;

	if (process_import_uri(s, &addr, &port, &uri))
	{
		fprintf(stderr, "error in processing import uri\n");
		return -1;
	}

	i = 0;

 	n = snprintf(&buf[i], sizeof (buf) - i,
		"POST %s HTTP/1.1" CRLF
		"Host: %s:%d" CRLF
		"transferMode.dlna.org: Background" CRLF
		"Expect: 100-continue" CRLF
		"contentFeatures.dlna.org: DLNA.ORG_PN=%s" CRLF
		"Connection: Close" CRLF
		"Content-Type: video/vnd.dlna.mpeg-tts" CRLF
		"Transfer-Encoding: chunked" CRLF
		"User-Agent: httppost" CRLF CRLF,
		 uri, addr, port, pn);
	if (n < 0 || n >= sizeof (buf) - i)
		return -1;
	i += n;

	sock = prepare_socket(addr, port);
	if (sock < 0)
		return -1;

	printf("%s", buf);
	send(sock, buf, i, 0);

	return sock;
}

static int encode_pcp(const char *buf, int len, char *outbuf)
{
	int padding = 16 - (len & 0x0f);
	int outlen;

	outlen = 10;
	memcpy(outbuf, "\x00" "\x54" "\x00\x00\x5b\xfb\x7a\xf1\x9c\x19", outlen);

	outbuf[outlen++] = (len >> 24) & 0xff;
	outbuf[outlen++] = (len >> 16) & 0xff;
	outbuf[outlen++] = (len >> 8) & 0xff;
	outbuf[outlen++] = (len >> 0) & 0xff;

	memcpy(&outbuf[outlen], buf, len);
	outlen += len;

	if (padding != 16)
	{
		memset(&outbuf[outlen], 0, padding);
		outlen += padding;
	}

	return outlen;
}

static int check_100_response(int sock)
{
	fd_set rfds, wfds;
	int n;
	char *p;
	int len;

	FD_ZERO(&rfds);
	FD_ZERO(&wfds);
	FD_SET(sock, &rfds);
	FD_SET(sock, &wfds);

	select(sock + 1, &rfds, &wfds, NULL, NULL);
	
	if (!FD_ISSET(sock, &rfds))
		return 0;

	p = recv_http_response(sock);
	if (!p)
		return 1;

	printf("\n%s", p);

	n = check_http_response(p, &len);
	if (n == 100)
	{
		free(p);

		if (len > 0)
		{
			p = recv_body(sock, len);
			if (!p)
				return 1;
			free(p);
		}

		return 0;
	}

	return 1;
}

static int post_image(int sock, const char *basename)
{
	static char outbuf[2 * (PCP_DATA_SIZE + PCP_HEADER_SIZE + PCP_MAX_PADDING)];
	int outbuf_len;
	FILE *fp;
	long long size;
	char *fname;
	struct timeval tv_s;
	struct timeval tv_e;
	int sec, u_sec;
	TS_FILTER *ts_filter;

	fname = malloc(strlen(basename) + strlen(IMG_EXT) + 1);
	if (!fname)
	{
		fprintf(stderr, "\nOut of memory\n");
		return 1;
	}

	strcpy(fname, basename);
	strcat(fname, IMG_EXT);

	fp = fopen(fname, "rb");
	free(fname);

	if (!fp)
	{
		fprintf(stderr, "\nOpen fail: %s\n", fname);
		return 1;
	}

	ts_filter = TS_FILTER_init();

	outbuf_len = 0;
	size = 0;
	while (1)
	{
		static char buf[PCP_DATA_SIZE];
		int n = 0;

		if (check_100_response(sock))
			break;

		if (size == 0)
			gettimeofday(&tv_s, NULL);

		printf("\r" I64d, size);
		fflush(stdout);

		if (outbuf_len < PCP_DATA_SIZE)
			n = fread(buf, 1, sizeof buf, fp);

		if (n > 0)
		{
			int nread = n;
			int nn;

			n = 0;
			for (nn = 0; nn < nread; nn += 192)
			{
				if (TS_FILTER_get(ts_filter, &buf[nn]))
				{
					fprintf(stderr, "TS_FILTER: Buffer overflow\n");

					fclose(fp);
					TS_FILTER_free(ts_filter);
					return 1;
				}

				if (ts_filter->buflen)
				{
					memcpy(&buf[n], ts_filter->buf, ts_filter->buflen);
					n += ts_filter->buflen;
				}
			}

			size += n;

			outbuf_len += encode_pcp(buf, n, &outbuf[outbuf_len]);
			if (outbuf_len > sizeof outbuf)
			{
				fprintf(stderr, "PCP: Buffer overflow!!\n");

				fclose(fp);
				TS_FILTER_free(ts_filter);
				return 1;
			}
		}

		if (n < PCP_DATA_SIZE)
		{
			if (outbuf_len > PCP_DATA_SIZE)
				n = PCP_DATA_SIZE;
			else
				n = outbuf_len;
		}

		sprintf(buf, "%x" CRLF, n);
		send(sock, buf, strlen(buf), 0);

		if (n > 0)
		{
			send(sock, outbuf, n, 0);
			memmove(outbuf, &outbuf[n], outbuf_len - n);
			outbuf_len -= n;
		}

		send(sock, CRLF, strlen(CRLF), 0);

		if (n == 0)
			break;
	}

	gettimeofday(&tv_e, NULL);

	TS_FILTER_free(ts_filter);

	if (!feof(fp) || outbuf_len > 0)
	{
		fprintf(stderr, "Unknown error!!\n");
		fclose(fp);
		return 1;
	}

	fclose(fp);

	sec = tv_e.tv_sec - tv_s.tv_sec;
	u_sec = tv_e.tv_usec - tv_s.tv_usec;
	if (u_sec < 0)
	{
		sec -= 1;
		u_sec += 1000000;
	}

	printf("\rtotal " I64d " bytes in %d.%06d sec (%.2lf MB/s)\n",
		size, sec, u_sec, (double)size / (sec + u_sec / 1000000.0) / 1024 / 1024);

	return 0;
}

int check_m2ts_file(const char *basename)
{
	FILE *fp;
	char *fname;
	int i;

	printf("Checking image file... ");
	fflush(stdout);

	fname = malloc(strlen(basename) + strlen(IMG_EXT) + 1);
	if (!fname)
	{
		fprintf(stderr, "\nOut of memory\n");
		return 1;
	}

	strcpy(fname, basename);
	strcat(fname, IMG_EXT);
printf("%s\n", fname);

	fp = fopen(fname, "rb");
	if (!fp)
	{
		fprintf(stderr, "\nOpen fail: %s\n", fname);
		free(fname);
		return 1;
	}

	free(fname);

	i = 0;
	while (!feof(fp))
	{
		char buf[192];
		int n;

		if (++i > 1024)
			break;

		n = fread(buf, 1, sizeof buf, fp);

		if (n == 0)
			break;

		if (n != 192)
			fprintf(stderr, "\nWorng image fle size\n");
		else if (buf[4] != 0x47)
			fprintf(stderr, "\nWorng image fle format\n");
		else
			continue;

		fclose(fp);
		return 1;
	}

	fclose(fp);

	printf("OK\n\n");

	return 0;
}

int check_post_image_result(int sock)
{
	int n = -1;

	while (1)
	{
		char *p;
		int len;

		p = recv_http_response(sock);
		if (!p)
			break;

		printf("%s", p);

		n = check_http_response(p, &len);
		free(p);

		if (len > 0)
		{
			p = recv_body(sock, len);
			if (!p)
				break;

			printf("%s\n", p);
			free(p);
		}

		if (n != 100)
			break;
	}

	return n;
}

int main(int argc, const char *argv[])
{
	const char *soap_addr;
	int soap_port;
	char *basename;
	char *pn;
	char *res;
	int sock;
	int n;

#if USE_WINAPI
	WSADATA	wsa_data;

	if (WSAStartup(MAKEWORD(2,2), &wsa_data))
	{
		fprintf(stderr, "ERROR: WSAStartup() fail");
		return 1;
	}
#endif /* USE_WINAPI */

	if (parse_args(&argv[1], &soap_addr, &soap_port, &basename, &pn))
	{
		fprintf(stderr, "Usage: %s <dest ip> [dest port]"
			" <m2ts image file> [dlna_org_pn]\n",
			argv[0]);
		return 1;
	}

	printf("SOAP addr = %s\n", soap_addr);
	printf("SOAP port = %d\n", soap_port);
	printf("image base = %s\n", basename);
	printf("DLNA.ORG_PN = %s\n", pn);
	printf("\n");

	if (check_m2ts_file(basename))
		return 1;

	res = make_create_req(basename, pn);
	if (!res)
	{
		fprintf(stderr, "Failed to create request\n");
		return 1;
	}

	printf("\n");

	sock = send_create_object(soap_addr, soap_port, res);
	free(res);

	if (sock < 0)
	{
		fprintf(stderr, "Failed to send request\n");
		return 1;
	}

	res = recv_create_object_response(sock);
	close(sock);

	if (!res)
	{
		fprintf(stderr, "Failed to recv result\n");
		return 1;
	}

	sock = send_post_header(res, pn);
	free(res);
	free(pn);

	if (sock < 0)
	{
		fprintf(stderr, "Failed to send post\n");
		return 1;
	}

	n = post_image(sock, basename);
	free(basename);

	if (n)
	{
		fprintf(stderr, "Failed to send image\n");
		close(sock);
		return 1;
	}

	n = check_post_image_result(sock);

	close(sock);

	if (n == 200 || n == 201)
		printf("Success\n");
	else
		printf("Failed\n");

#if USE_WINAPI
	WSACleanup();
#endif /* USE_WINAPI */

	return 0;
}
