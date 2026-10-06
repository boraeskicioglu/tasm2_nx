/* Original setup/storage bridge. Expansion revisions come from 1.2.8d native strings. */
#include "config.h"
#include "dcr_manifest.h"
#include "dcr_path.h"
#include "dcr_prefs.h"
#include "dcr_setup.h"
#include "error.h"
#include "tasm2.h"
#include "util.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include <sys/stat.h>
static char obb_path[3][768], obb_name[3][160];
static mz_zip_archive apk;
static Mutex apk_lock;
static int apk_open;
int tasm2_signature_hash(void);
static const char *const libs[] = {"libgenerator.so", "libtasm2.so"};
const RtSetupPlan port_setup_plan = {
    .libs = libs, .nlibs = 2, .libs_p0 = 0, .libs_p1 = 800, .classes_p0 = 800, .classes_p1 = 990};
const char *port_apk_help(void) {
  return "Copy your own ARMv7 1.2.8d APK (version code 12723) and both OBBs to "
         "sd:/switch/tasm2_nx/. See README.md.";
}
static void find_obbs(void) {
  const char *dirs[] = {"", "obb", PORT_PACKAGE, "Android/obb/" PORT_PACKAGE,
                        "sdcard/Android/obb/" PORT_PACKAGE};
  for (unsigned k = 0; k < sizeof dirs / sizeof dirs[0] + 1; k++) {
    char dir[768];
    if (k == sizeof dirs / sizeof dirs[0])
      snprintf(dir, sizeof dir, "sdmc:/Android/obb/" PORT_PACKAGE);
    else
      snprintf(dir, sizeof dir, "%s/%s", dcr_game_root(), dirs[k]);
    DIR *d = opendir(dir);
    if (!d)
      continue;
    struct dirent *e;
    while ((e = readdir(d))) {
      unsigned rev = 0;
      char type[12], pkg[128];
      if (sscanf(e->d_name, "%11[^.].%u.%127[^ ]", type, &rev, pkg) != 3)
        continue;
      if (strcmp(pkg, PORT_PACKAGE ".obb"))
        continue;
      int idx = !strcmp(type, "main") ? 0 : !strcmp(type, "patch") ? (rev == 12438u ? 2 : 1) : -1;
      if (idx < 0 || rev != (idx == 2 ? 12438u : idx == 1 ? 12723u : 12032u))
        continue;
      char path[768];
      if (snprintf(path, sizeof path, "%s/%s", dir, e->d_name) >= (int)sizeof path ||
          strlen(e->d_name) >= sizeof obb_name[0])
        continue;
      struct stat st;
      if (stat(path, &st) || !S_ISREG(st.st_mode))
        continue;
      /* These audited expansion files have fixed sizes; truncated transfers must not boot. */
      uint64_t want = idx == 2 ? 0 : idx == 1 ? 98576965ull : 1236510228ull;
      if (want && (uint64_t)st.st_size != want) {
        debugPrintf("[setup] wrong size: %s (%llu, need %llu)\n", path,
                    (unsigned long long)st.st_size, (unsigned long long)want);
        continue;
      }
      if (!*obb_path[idx]) {
        snprintf(obb_path[idx], sizeof obb_path[idx], "%s", path);
        strcpy(obb_name[idx], e->d_name);
      }
    }
    closedir(d);
  }
  for (int i = 0; i < 2; i++)
    if (!*obb_path[i])
      fatal_error("Missing or truncated %s OBB for TASM2 1.2.8d.\n\nRequired: %s.%u." PORT_PACKAGE
                  ".obb\nCopy it into sd:/switch/tasm2_nx/ or its obb/ folder.",
                  i ? "patch" : "main", i ? "patch" : "main", i ? 12723 : 12032);
  debugPrintf("[setup] direct OBB reader: %s; %s\n", obb_path[0], obb_path[1]);
  DcrPrefsEdit *e = dcr_prefs_edit(dcr_prefs_open("ExpansionPrefs"));
  dcr_prefs_put(e, 's', "MainFileName", obb_name[0]);
  dcr_prefs_put(e, 's', "PatchFileName", obb_name[1]);
  dcr_prefs_commit(e);
  dcr_prefs_edit_free(e);
  e = dcr_prefs_edit(dcr_prefs_open("GamePrefs"));
  dcr_prefs_put(e, 's', "SDFolder", A_RESOURCE);
  dcr_prefs_commit(e);
  dcr_prefs_edit_free(e);
}
void tasm2_storage_init(const char *path) {
  if (strcmp(dcr_manifest_version_name(), "1.2.8d") || dcr_manifest_version_code() != 12723)
    fatal_error("Unsupported APK: %s (code %d).\nUse ARMv7 TASM2 1.2.8d (code 12723).",
                dcr_manifest_version_name(), dcr_manifest_version_code());
  find_obbs();
  dcr_setup_from_apk(path);
  const uint32_t crc_expected[] = {0x337cc20du, 0xdf28ab56u};
  for (unsigned i = 0; i < 2; i++) {
    char library[768];
    rt_root_path(library, sizeof library, libs[i]);
    FILE *f = fopen(library, "rb");
    if (!f)
      fatal_error("Cannot read %s.", libs[i]);
    uint32_t crc = 0;
    uint8_t block[16384];
    size_t n;
    while ((n = fread(block, 1, sizeof block, f)))
      crc = mz_crc32(crc, block, n);
    int err = ferror(f);
    fclose(f);
    if (err || crc != crc_expected[i])
      fatal_error("Unsupported or damaged %s. Restore the original 1.2.8d APK and remove extracted "
                  "libraries before setup.",
                  libs[i]);
    debugPrintf("[setup] %s original ARMv7 binary verified\n", libs[i]);
  }
  memset(&apk, 0, sizeof apk);
  if (!mz_zip_reader_init_file(&apk, path, 0))
    fatal_error("Cannot read the APK's assets.");
  apk_open = 1;
  /* Cache immutable certificate identity before any guest worker can query it. */
  tasm2_signature_hash();
}
const char *tasm2_storage_obb_path(int i) {
  return i >= 0 && i < 3 ? obb_path[i] : NULL;
}
const char *port_path_fixup(const char *real, char *out, size_t cap) {
  const char *base = strrchr(real, '/');
  base = base ? base + 1 : real;
  for (int i = 0; i < 3; i++)
    if (*obb_name[i] && !strcmp(base, obb_name[i])) {
      snprintf(out, cap, "%s", obb_path[i]);
      return out;
    }
  return real;
}
uint8_t *tasm2_catalog_read(size_t *);
uint8_t *tasm2_apk_asset(const char *name, size_t *len) {
  if (len)
    *len = 0;
  char entry[768];
  while (*name == '/' || *name == '.')
    name++;
  snprintf(entry, sizeof entry, "%s%s", strncmp(name, "assets/", 7) ? "assets/" : "", name);
  mutexLock(&apk_lock);
  if (!apk_open) {
    mutexUnlock(&apk_lock);
    return NULL;
  }
  size_t n = 0;
  void *p = mz_zip_reader_extract_file_to_heap(&apk, entry, &n, 0);
  mutexUnlock(&apk_lock);
  if (!p && !strcmp(entry, "assets/IapLocalData_Google.json"))
    return tasm2_catalog_read(len);
  if (len)
    *len = n;
  return p;
}
/* Read the actual signing certificate from PKCS#7 rather than inventing a signature. */
static int der(const uint8_t *p, size_t n, unsigned *tag, size_t *hdr, size_t *len) {
  if (n < 2)
    return 0;
  *tag = p[0];
  size_t h = 2, v = p[1];
  if (v & 128) {
    unsigned k = v & 127;
    if (!k || k > 4 || n < 2 + k)
      return 0;
    v = 0;
    for (unsigned i = 0; i < k; i++)
      v = (v << 8) | p[2 + i];
    h += k;
  }
  if (v > n - h)
    return 0;
  *hdr = h;
  *len = v;
  return 1;
}
int tasm2_signature_hash(void) {
  static int ready, hash;
  if (ready)
    return hash;
  mutexLock(&apk_lock);
  unsigned count = mz_zip_reader_get_num_files(&apk);
  size_t size = 0;
  uint8_t *pkcs = NULL;
  for (unsigned i = 0; i < count; i++) {
    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(&apk, i, &st))
      continue;
    size_t l = strlen(st.m_filename);
    if (!strncmp(st.m_filename, "META-INF/", 9) && l > 4 &&
        !strcmp(st.m_filename + l - 4, ".RSA")) {
      pkcs = mz_zip_reader_extract_to_heap(&apk, i, &size, 0);
      break;
    }
  }
  mutexUnlock(&apk_lock);
  if (pkcs) {
    unsigned tag;
    size_t h, n;
    const uint8_t *v = pkcs;
    size_t left = size;
    /* ContentInfo SEQUENCE, OID then explicit signedData [0], then its SEQUENCE. */
    if (der(v, left, &tag, &h, &n) && tag == 0x30) {
      v += h;
      left = n;
      if (der(v, left, &tag, &h, &n)) {
        v += h + n;
        left -= h + n;
        if (der(v, left, &tag, &h, &n) && tag == 0xa0) {
          v += h;
          left = n;
          if (der(v, left, &tag, &h, &n) && tag == 0x30) {
            v += h;
            left = n;
            /* version INTEGER, digestAlgorithms SET, encapContentInfo SEQUENCE. */
            int ok = 1;
            for (int i = 0; i < 3; i++) {
              if (!der(v, left, &tag, &h, &n)) {
                ok = 0;
                break;
              }
              v += h + n;
              left -= h + n;
            }
            if (ok && der(v, left, &tag, &h, &n) && tag == 0xa0) {
              v += h;
              left = n;
              if (der(v, left, &tag, &h, &n) && tag == 0x30) {
                uint32_t result = 1;
                for (size_t i = 0; i < h + n; i++)
                  result = result * 31 + (int8_t)v[i];
                hash = (int32_t)result;
                ready = 1;
              }
            }
          }
        }
      }
    }
    free(pkcs);
  }
  if (!ready)
    fatal_error("Could not read the APK signing certificate. Use your original, signed APK.");
  debugPrintf("[setup] APK certificate hashCode %d\n", hash);
  return hash;
}
void tasm2_storage_close(void) {
  mutexLock(&apk_lock);
  if (apk_open)
    mz_zip_reader_end(&apk);
  apk_open = 0;
  mutexUnlock(&apk_lock);
} /* Catalog is a small plain-text member inside this verified main OBB. */
uint8_t *tasm2_catalog_read(size_t *len) {
  FILE *f = fopen(obb_path[0], "rb");
  if (!f)
    return NULL;
  uint8_t *data = malloc(9578);
  if (!data) {
    fclose(f);
    return NULL;
  }
  int ok = fseek(f, 312677714L, SEEK_SET) == 0 && fread(data, 1, 9577, f) == 9577;
  fclose(f);
  if (!ok || mz_crc32(0, data, 9577) != 0x036b8ff1u) {
    free(data);
    debugPrintf("[setup] expansion's original catalog failed CRC; storefront unavailable\n");
    return NULL;
  }
  data[9577] = 0;
  if (len)
    *len = 9577;
  return data;
}
