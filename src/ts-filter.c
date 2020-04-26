#include <stdlib.h>
#include <string.h>
#include "ts-filter.h"

struct TS_FILTER_Priv
{
	char work[PACKET_SIZE];
};

TS_FILTER *TS_FILTER_init(void)
{
	TS_FILTER *ts_filter;
	struct TS_FILTER_Priv *priv;

	ts_filter = malloc(sizeof *ts_filter);
	if (!ts_filter)
		return NULL;

	priv = malloc(sizeof *priv);
	if (!priv)
	{
		free(ts_filter);
		return NULL;
	}

	memset(ts_filter, 0, sizeof *ts_filter);
	memset(priv, 0, sizeof *priv);

	ts_filter->priv = priv;

	return ts_filter;
}

void TS_FILTER_free(TS_FILTER *ts_filter)
{
	free(ts_filter->priv);
	free(ts_filter);
}

int TS_FILTER_get(TS_FILTER *ts_filter, const char *in)
{
	memcpy(ts_filter->priv->work, in, sizeof ts_filter->priv->work);

	;

	ts_filter->buf = ts_filter->priv->work;
	ts_filter->buflen = sizeof ts_filter->priv->work;

	return 0;
}
