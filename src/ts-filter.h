#ifndef __TS_FILTER_H__
#define __TS_FILTER_H__

#define PACKET_SIZE	(192)

struct TS_FILTER_Priv;
typedef struct
{
	const char *buf;
	int buflen;

	struct TS_FILTER_Priv *priv;
} TS_FILTER;

TS_FILTER *TS_FILTER_init(void);
void TS_FILTER_free(TS_FILTER *ts_filter);
int TS_FILTER_get(TS_FILTER *ts_filter, const char *in);

#endif /* __TS_FILTER_H__ */
