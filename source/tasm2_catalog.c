/* Offline UI/product metadata. Billing still reports unavailable purchases. */
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define JSMN_STATIC
#include "config.h"
#include "dcr_setup.h"
#include "thirdparty/jsmn.h"
#include "util.h"
#include <stdint.h>
#include <switch.h>
uint8_t *tasm2_catalog_read(size_t *);
const char *tasm2_shop_catalog_json(void);
static int skip(const jsmntok_t *t, int n, int i) {
  int end = t[i].end;
  for (i++; i < n && t[i].start < end; i++)
    ;
  return i;
}
static int member(const char *s, const jsmntok_t *t, int n, int obj, const char *key) {
  if (obj < 0 || obj >= n || t[obj].type != JSMN_OBJECT)
    return -1;
  for (int i = obj + 1; i < n && t[i].start < t[obj].end;) {
    int v = i + 1;
    if (v >= n)
      return -1;
    if (t[i].type == JSMN_STRING && t[i].end - t[i].start == (int)strlen(key) &&
        !memcmp(s + t[i].start, key, strlen(key)))
      return v;
    i = skip(t, n, v);
  }
  return -1;
}
static int emit(char *dst, size_t cap, size_t *used, const char *s, const jsmntok_t *t, int index) {
  if (index < 0)
    return 0;
  int begin = t[index].start, end = t[index].end;
  if (t[index].type == JSMN_STRING) {
    begin--;
    end++;
  }
  size_t len = end - begin;
  if (*used + len + 1 >= cap)
    return 0;
  memcpy(dst + *used, s + begin, len);
  *used += len;
  dst[*used] = 0;
  return 1;
}
static int text(char *dst, size_t cap, size_t *used, const char *s) {
  size_t n = strlen(s);
  if (*used + n + 1 >= cap)
    return 0;
  memcpy(dst + *used, s, n + 1);
  *used += n;
  return 1;
}
const char *tasm2_catalog_json(void) {
  static char *cached;
  static Mutex cache_lock;
  mutexLock(&cache_lock);
  if (cached) {
    mutexUnlock(&cache_lock);
    return cached;
  }
  const uint8_t *bytes = (const uint8_t *)tasm2_shop_catalog_json();
  size_t size = bytes ? strlen((const char *)bytes) : 0;
  if (!bytes) {
    mutexUnlock(&cache_lock);
    return "{\"store_items\":[]}";
  }
  jsmntok_t tokens[1024];
  jsmn_parser parser;
  jsmn_init(&parser);
  int n = jsmn_parse(&parser, (char *)bytes, size, tokens, 1024);
  char *out = calloc(1, 16384);
  size_t used = 0;
  int count = 0, ok = n > 0 && out;
  if (ok) {
    int items = member((char *)bytes, tokens, n, 0, "items");
    ok = items >= 0 && tokens[items].type == JSMN_ARRAY;
    if (ok) {
      text(out, 16384, &used, "{\"store_items\":[");
      for (int item = items + 1; item < n && tokens[item].start < tokens[items].end && ok;
           item = skip(tokens, n, item)) {
        int b = member((char *)bytes, tokens, n, item, "billing_methods");
        if (b < 0 || tokens[b].type != JSMN_ARRAY) {
          ok = 0;
          break;
        }
        int method = b + 1;
        if (method >= n || tokens[method].start >= tokens[b].end ||
            tokens[method].type != JSMN_OBJECT) {
          ok = 0;
          break;
        }
        int price = member((char *)bytes, tokens, n, method, "price");
        if (price < 0) {
          ok = 0;
          break;
        }
        char amount[48];
        int len = tokens[price].end - tokens[price].start;
        if (len <= 0 || len > 40) {
          ok = 0;
          break;
        }
        memcpy(amount, bytes + tokens[price].start, len);
        amount[len] = 0;
        char *end;
        errno = 0;
        double price_value = strtod(amount, &end);
        if (errno || *end || !isfinite(price_value) || price_value < 0 || price_value > 1e9) {
          ok = 0;
          break;
        }
        double micros = round(price_value * 1000000.0);
        char formatted[96];
        snprintf(formatted, sizeof formatted, "%s{\"type\":\"inapp\",\"price_amount_micros\":%.0f",
                 count ? "," : "", micros);
        ok = text(out, 16384, &used, formatted);
        const char *keys[] = {"title", "price", "description", "productId", "price_currency_code"};
        const char *src[] = {"name", "display_price", "description", "entry_id", "currency"};
        for (int k = 0; k < 5 && ok; k++) {
          snprintf(formatted, sizeof formatted, ",\"%s\":", keys[k]);
          ok = text(out, 16384, &used, formatted) &&
               emit(out, 16384, &used, (char *)bytes, tokens,
                    member((char *)bytes, tokens, n, (k == 1 || k == 4) ? method : item, src[k]));
        }
        if (ok)
          ok = text(out, 16384, &used, "}");
        count++;
      }
      if (ok)
        ok = text(out, 16384, &used, "]}");
    }
  }
  if (!ok) {
    free(out);
    debugPrintf("[online] catalog conversion failed; storefront unavailable\n");
    mutexUnlock(&cache_lock);
    return "{\"store_items\":[]}";
  }
  cached = out;
  debugPrintf("[online] %d products: prices from owned OBB, purchases unavailable\n", count);
  mutexUnlock(&cache_lock);
  return cached;
}
/* The original Google catalog omits Scarlet although ItemData, IapProfileMap
 * and ShopItemPriority explicitly include it. Add a local UI descriptor using
 * those verified names. The native parser discards zero-priced rows, so the
 * internal price is a positive placeholder and the display reads "Owned".
 * All costume ownership is local; billing always fails without a receipt. */
const char *tasm2_shop_catalog_json(void) {
  static char *cached;
  static Mutex lock;
  mutexLock(&lock);
  if (!cached) {
    size_t size=0;uint8_t *raw=tasm2_catalog_read(&size);
    jsmntok_t tokens[1024];jsmn_parser parser;jsmn_init(&parser);
    int n=raw?jsmn_parse(&parser,(const char *)raw,size,tokens,1024):-1;
    int items=n>0?member((const char *)raw,tokens,n,0,"items"):-1;
    static const char scarlet[] =
      ",{" "\"entry_id\":\"Scarlet_Spider_suit_offline\","
      "\"item\":\"Suit_Sca\",\"quantity\":1,\"hidden\":false,"
      "\"name\":\"Scarlet Spider\",\"description\":\"Scarlet Spider\",\"tier\":\"Offline\",\"locale\":\"en_US@currency=USD\","
      "\"icon\":\"icon_not_available.jpg\",\"billing_methods\":[{"
      "\"name\":\"googleplay\",\"type\":\"googleplay\",\"currency\":\"USD\","
      "\"currency_symbol\":\"$\",\"price\":0.01,\"display_price\":\"Owned\","
      "\"content_id\":\"offline.Scarlet_Spider_suit\"}]}";
    if (items>=0 && tokens[items].type==JSMN_ARRAY && tokens[items].size>0) {
      size_t insert=(size_t)tokens[items].end-1;
      cached=malloc(size+sizeof scarlet);
      if (cached) {
        memcpy(cached,raw,insert);
        memcpy(cached+insert,scarlet,sizeof scarlet-1);
        memcpy(cached+insert+sizeof scarlet-1,raw+insert,size-insert);
        cached[size+sizeof scarlet-1]=0;
      }
    }
    free(raw);
  }
  mutexUnlock(&lock);
  return cached;
}
