#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jbig2.h"

typedef struct _Jbig2SegmentHeader Jbig2SegmentHeader;
typedef struct _Jbig2SymbolDictionary Jbig2SymbolDictionary;

struct _Jbig2Ctx {
  FILE *f;
  int offset;

  byte flags;
  int32 n_pages;
};

struct _Jbig2SegmentHeader {
  int32 segment_number;
  byte flags;
  int referred_to_segment_count;
  int32 page_association;
  int data_length;
};

struct _Jbig2SymbolDictionary {
  int16 flags;
  int8 SDAT_flags[8];
  byte SDRAT_flags[4];
  int32 SDNUMEXSYMS;
  int32 SDNUMNEWSYMS;
};

int32
get_bytes (Jbig2Ctx *ctx, byte *buf, int size, int off)
{
  fseek (ctx->f, off, SEEK_SET);
  return fread (buf, 1, size, ctx->f);
}

int16
get_int16 (Jbig2Ctx *ctx, int off)
{
  byte buf[2];

  get_bytes (ctx, buf, 2, off);
  return (buf[0] << 8) | buf[1];
}

int32
get_int32 (Jbig2Ctx *ctx, int off)
{
  byte buf[4];

  get_bytes (ctx, buf, 4, off);
  return (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
}

static Jbig2Ctx *
jbig2_open (FILE *f)
{
  byte buf[9];
  const byte header[8] = { 0x97, 0x4a, 0x42, 0x32, 0x0d, 0x0a, 0x1a, 0x0a };
  Jbig2Ctx *ctx;

  /* Annex D.4 */
  ctx = (Jbig2Ctx *)malloc (sizeof(Jbig2Ctx));
  ctx->f = f;
  get_bytes (ctx, buf, 9, 0);
  if (memcmp (buf, header, 8))
    {
      printf ("not a JBIG2 file\n");
      return NULL;
    }
  ctx->flags = buf[8];
  if (ctx->flags & 2)
    {
      ctx->offset = 9;
    }
  else
    {
      ctx->offset = 13;
      ctx->n_pages = get_int32 (ctx, 9);
    }
  return ctx;
}

static Jbig2SegmentHeader *
jbig2_read_segment_header (Jbig2Ctx *ctx)
{
  Jbig2SegmentHeader *result = (Jbig2SegmentHeader *)malloc(sizeof(Jbig2SegmentHeader));
  int32 offset = ctx->offset;
  byte rtscarf;
  int referred_to_segment_count;
  byte spa;

  /* 7.2.2 */
  result->segment_number = get_int32 (ctx, offset);

  /* 7.2.3 */
  get_bytes (ctx, &result->flags, 1, offset + 4);

  /* 7.2.4 */
  get_bytes (ctx, &rtscarf, 1, offset + 5);
  if ((rtscarf & 0xe0) == 0xe0)
    {
      printf ("long form of rtscarf, can't handle!\n");
    }
  else
    {
      referred_to_segment_count = (rtscarf >> 5);
      offset += 6 + referred_to_segment_count;
    }
  result->referred_to_segment_count = referred_to_segment_count;
  /* todo: read referred to segment numbers */

  /* 7.2.6 */
  get_bytes (ctx, &spa, 1, offset);
  if (result->flags & 64)
    {
      printf ("long form of spa, can't handle!\n");
    }
  result->page_association = spa;

  /* 7.2.7 */
  result->data_length = get_int32 (ctx, offset + 1);
  ctx->offset = offset + 5;

  return result;
}

static Jbig2SymbolDictionary *
jbig2_read_symbol_dictionary (Jbig2Ctx *ctx)
{
  Jbig2SymbolDictionary *result = (Jbig2SymbolDictionary *)malloc(sizeof(Jbig2SymbolDictionary));
  int32 offset = ctx->offset;
  bool SDHUFF, SDREFAGG, SDRTEMPLATE;
  int sdat_bytes;

  /* 7.4.2.1.1 */
  result->flags = get_int16 (ctx, offset);
  offset += 2;

  SDHUFF = result->flags & 1;
  SDREFAGG = (result->flags >> 1) & 1;
  SDRTEMPLATE = (result->flags >> 12) & 1;

  /* 7.4.2.1.2 */
  if (!SDHUFF)
    {
      int SDTEMPLATE = (result->flags >> 10) & 3;
      if (SDTEMPLATE == 0)
	sdat_bytes = 8;
      else
	sdat_bytes = 2;
    }
  else
    sdat_bytes = 0;
  get_bytes (ctx, result->SDAT_flags, sdat_bytes, offset);
  memset (&result->SDAT_flags + sdat_bytes, 0, 8 - sdat_bytes);
  offset += sdat_bytes;

  /* 7.4.2.1.3 */
  if (SDREFAGG && !SDRTEMPLATE)
    {
      get_bytes (ctx, result->SDRAT_flags, 4, offset);
      offset += 4;
    }

  /* 7.4.2.1.4 */
  result->SDNUMEXSYMS = get_int32 (ctx, offset);

  /* 7.4.2.1.5 */
  result->SDNUMNEWSYMS = get_int32 (ctx, offset + 4);
  offset += 8;

  return result;
}

static void
dump_symbol_dictionary (Jbig2SymbolDictionary *sd)
{
  printf ("segment type = symbol dictionary, flags = %04x, numexsyms = %d, numnewsyms = %d\n",
	  sd->flags, sd->SDNUMEXSYMS, sd->SDNUMNEWSYMS);
}

static bool
dump_segment (Jbig2Ctx *ctx)
{
  Jbig2SegmentHeader *sh;
  int32 offset;
  Jbig2SymbolDictionary *sd;

  sh = jbig2_read_segment_header (ctx);
  offset = ctx->offset;
  printf ("segment number = %d, flags = %02x, page %d, %d bytes\n",
	  sh->segment_number, sh->flags, sh->page_association, sh->data_length);
  switch (sh->flags & 63)
    {
    case 0:
      sd = jbig2_read_symbol_dictionary (ctx);
      dump_symbol_dictionary (sd);
      break;
    case 51:
      printf ("segment type = end of file\n");
      return TRUE;
    }
  ctx->offset = offset + sh->data_length;
  return FALSE;
}

static void
dump_jbig2 (FILE *f)
{
  Jbig2Ctx *ctx;
  bool last;

  ctx = jbig2_open (f);
  printf ("Number of pages = %d\n", ctx->n_pages);
  for (;;)
    {
      last = dump_segment (ctx);
      if (last)
	break;
    }
}

int
main (int argc, char **argv)
{
  FILE *f;

  if (argc < 2)
    return -1;
  f = fopen (argv[1], "rb");
  if (f == NULL)
    return -1;

  dump_jbig2 (f);
  fclose (f);
  return 0;
}
