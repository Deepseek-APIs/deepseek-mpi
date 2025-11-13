#define _GNU_SOURCE
#include "attachment_loader.h"

#include "string_buffer.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_LIBMAGIC
#include <magic.h>
#endif

#ifdef HAVE_LIBXML2
#include <libxml/parser.h>
#include <libxml/tree.h>
#endif

#ifdef HAVE_LIBARCHIVE
#include <archive.h>
#include <archive_entry.h>
#endif

#ifdef HAVE_POPPLER_GLIB
#include <glib.h>
#include <poppler.h>
#endif

#if defined(HAVE_POPPLER_GLIB) && defined(HAVE_TESSERACT)
#include <cairo/cairo.h>
#endif

#ifdef HAVE_TESSERACT
#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>
#endif

typedef enum {
  DATA_CLASS_TEXT,
  DATA_CLASS_BINARY
} DataClass;

static void assign_error(char **error_out, const char *fmt, ...) {
  if (!error_out) {
    return;
  }
  va_list args;
  va_start(args, fmt);
  char *msg = NULL;
  if (vasprintf(&msg, fmt, args) < 0) {
    msg = NULL;
  }
  va_end(args);
  *error_out = msg;
}

static int read_all_bytes(const char *path, unsigned char **out, size_t *len, char **error_out) {
  FILE *fp = fopen(path, "rb");
  if (!fp) {
    assign_error(error_out, "unable to open %s: %s", path, strerror(errno));
    return -1;
  }
  if (fseek(fp, 0, SEEK_END) != 0) {
    assign_error(error_out, "fseek failed for %s", path);
    fclose(fp);
    return -1;
  }
  long size = ftell(fp);
  if (size < 0) {
    assign_error(error_out, "ftell failed for %s", path);
    fclose(fp);
    return -1;
  }
  rewind(fp);
  unsigned char *buffer = malloc((size_t) size + 1);
  if (!buffer) {
    assign_error(error_out, "unable to allocate %ld bytes", size);
    fclose(fp);
    return -1;
  }
  size_t read_bytes = fread(buffer, 1, (size_t) size, fp);
  fclose(fp);
  if (read_bytes != (size_t) size) {
    assign_error(error_out, "short read for %s", path);
    free(buffer);
    return -1;
  }
  buffer[read_bytes] = '\0';
  *out = buffer;
  if (len) {
    *len = read_bytes;
  }
  return 0;
}

static DataClass classify_buffer(const unsigned char *data, size_t len) {
  size_t binary = 0;
  for (size_t i = 0; i < len; ++i) {
    unsigned char ch = data[i];
    if (ch == '\n' || ch == '\r' || ch == '\t') {
      continue;
    }
    if (ch < 0x09 || (ch > 0x0D && ch < 0x20) || ch == 0x7F) {
      binary++;
      if (binary * 5 > len) {
        return DATA_CLASS_BINARY;
      }
    }
  }
  return DATA_CLASS_TEXT;
}

static char *base64_encode(const unsigned char *data, size_t len) {
  static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t out_len = 4 * ((len + 2) / 3);
  char *out = malloc(out_len + 1);
  if (!out) {
    return NULL;
  }
  size_t i = 0, j = 0;
  while (i < len) {
    uint32_t octet_a = i < len ? data[i++] : 0;
    uint32_t octet_b = i < len ? data[i++] : 0;
    uint32_t octet_c = i < len ? data[i++] : 0;
    uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;
    out[j++] = table[(triple >> 18) & 0x3F];
    out[j++] = table[(triple >> 12) & 0x3F];
    out[j++] = (i > len + 1) ? '=' : table[(triple >> 6) & 0x3F];
    out[j++] = (i > len) ? '=' : table[triple & 0x3F];
  }
  out[out_len] = '\0';
  return out;
}

static const char *extension_label(const char *path) {
  const char *dot = strrchr(path ? path : "", '.');
  return dot ? dot + 1 : "";
}

static const char *fallback_mime_from_ext(const char *path) {
  const char *ext = extension_label(path);
  if (!ext || !*ext) {
    return "application/octet-stream";
  }
  if (!strcasecmp(ext, "txt") || !strcasecmp(ext, "md")) {
    return "text/plain";
  }
  if (!strcasecmp(ext, "html") || !strcasecmp(ext, "htm")) {
    return "text/html";
  }
  if (!strcasecmp(ext, "xml")) {
    return "application/xml";
  }
  if (!strcasecmp(ext, "json")) {
    return "application/json";
  }
  if (!strcasecmp(ext, "csv")) {
    return "text/csv";
  }
  if (!strcasecmp(ext, "png")) {
    return "image/png";
  }
  if (!strcasecmp(ext, "jpg") || !strcasecmp(ext, "jpeg")) {
    return "image/jpeg";
  }
  if (!strcasecmp(ext, "gif")) {
    return "image/gif";
  }
  if (!strcasecmp(ext, "bmp")) {
    return "image/bmp";
  }
  if (!strcasecmp(ext, "tiff") || !strcasecmp(ext, "tif")) {
    return "image/tiff";
  }
  if (!strcasecmp(ext, "pdf")) {
    return "application/pdf";
  }
  if (!strcasecmp(ext, "docx")) {
    return "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
  }
  if (!strcasecmp(ext, "xlsx")) {
    return "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";
  }
  return "application/octet-stream";
}

static const char *detect_mime_type(const char *path, const unsigned char *data, size_t len, char **magic_error) {
#ifdef HAVE_LIBMAGIC
  magic_t cookie = magic_open(MAGIC_MIME_TYPE);
  if (!cookie) {
    if (magic_error) {
      *magic_error = strdup("magic_open failed");
    }
    return fallback_mime_from_ext(path);
  }
  if (magic_load(cookie, NULL) != 0) {
    if (magic_error) {
      *magic_error = strdup(magic_error(cookie));
    }
    magic_close(cookie);
    return fallback_mime_from_ext(path);
  }
  const char *type = path ? magic_file(cookie, path) : NULL;
  if (!type && data && len > 0) {
    type = magic_buffer(cookie, data, len);
  }
  char *result = type ? strdup(type) : NULL;
  if (!result) {
    result = strdup(fallback_mime_from_ext(path));
  }
  magic_close(cookie);
  return result;
#else
  (void) data;
  (void) len;
  (void) magic_error;
  return strdup(fallback_mime_from_ext(path));
#endif
}

static int mime_is_textual(const char *mime) {
  if (!mime) {
    return 0;
  }
  if (!strncmp(mime, "text/", 5)) {
    return 1;
  }
  if (strstr(mime, "xml") || strstr(mime, "json") || strstr(mime, "yaml") || strstr(mime, "javascript")) {
    return 1;
  }
  return 0;
}

#if defined(HAVE_LIBARCHIVE) && defined(HAVE_LIBXML2)
static int extract_member(const char *path, const char *member, unsigned char **out, size_t *len) {
  struct archive *a = archive_read_new();
  archive_read_support_format_zip(a);
  if (archive_read_open_filename(a, path, 8192) != ARCHIVE_OK) {
    archive_read_free(a);
    return -1;
  }
  struct archive_entry *entry;
  while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
    const char *name = archive_entry_pathname(entry);
    if (name && strcmp(name, member) == 0) {
      size_t size = (size_t) archive_entry_size(entry);
      unsigned char *buffer = malloc(size + 1);
      if (!buffer) {
        archive_read_free(a);
        return -1;
      }
      ssize_t r = archive_read_data(a, buffer, size);
      archive_read_free(a);
      if (r < 0) {
        free(buffer);
        return -1;
      }
      buffer[r] = '\0';
      *out = buffer;
      if (len) {
        *len = (size_t) r;
      }
      return 0;
    }
    archive_read_data_skip(a);
  }
  archive_read_free(a);
  return -1;
}

static void xml_append_text(xmlNode *node, StringBuffer *sb) {
  for (xmlNode *cur = node; cur; cur = cur->next) {
    if (cur->type == XML_TEXT_NODE) {
      sb_append_str(sb, (const char *) cur->content);
    }
    xml_append_text(cur->children, sb);
    if (cur->type == XML_ELEMENT_NODE && (!xmlStrcmp(cur->name, (const xmlChar *) "p") ||
                                          !xmlStrcmp(cur->name, (const xmlChar *) "row"))) {
      sb_append_char(sb, '\n');
    }
  }
}

static char *extract_docx_text(const char *path) {
  unsigned char *xml_data = NULL;
  size_t len = 0;
  if (extract_member(path, "word/document.xml", &xml_data, &len) != 0) {
    return NULL;
  }
  xmlDocPtr doc = xmlReadMemory((const char *) xml_data, (int) len, "docx", NULL, XML_PARSE_RECOVER);
  free(xml_data);
  if (!doc) {
    return NULL;
  }
  xmlNode *root = xmlDocGetRootElement(doc);
  StringBuffer sb;
  sb_init(&sb);
  xml_append_text(root, &sb);
  xmlFreeDoc(doc);
  return sb_detach(&sb);
}

static char *extract_xlsx_text(const char *path) {
  unsigned char *xml_data = NULL;
  size_t len = 0;
  if (extract_member(path, "xl/sharedStrings.xml", &xml_data, &len) != 0) {
    return NULL;
  }
  xmlDocPtr doc = xmlReadMemory((const char *) xml_data, (int) len, "xlsx", NULL, XML_PARSE_RECOVER);
  free(xml_data);
  if (!doc) {
    return NULL;
  }
  xmlNode *root = xmlDocGetRootElement(doc);
  StringBuffer sb;
  sb_init(&sb);
  xml_append_text(root, &sb);
  xmlFreeDoc(doc);
  return sb_detach(&sb);
}

static char *extract_odf_text(const char *path) {
  unsigned char *xml_data = NULL;
  size_t len = 0;
  if (extract_member(path, "content.xml", &xml_data, &len) != 0) {
    return NULL;
  }
  xmlDocPtr doc = xmlReadMemory((const char *) xml_data, (int) len, "odf", NULL, XML_PARSE_RECOVER);
  free(xml_data);
  if (!doc) {
    return NULL;
  }
  xmlNode *root = xmlDocGetRootElement(doc);
  StringBuffer sb;
  sb_init(&sb);
  xml_append_text(root, &sb);
  xmlFreeDoc(doc);
  return sb_detach(&sb);
}

static char *extract_office_like_text(const char *path, const char *ext) {
  if (!ext) {
    return NULL;
  }
  if (!strcasecmp(ext, "docx") || !strcasecmp(ext, "docm") || !strcasecmp(ext, "dotx") ||
      !strcasecmp(ext, "dotm")) {
    return extract_docx_text(path);
  }
  if (!strcasecmp(ext, "xlsx") || !strcasecmp(ext, "xlsm") || !strcasecmp(ext, "xltx") ||
      !strcasecmp(ext, "xltm")) {
    return extract_xlsx_text(path);
  }
  if (!strcasecmp(ext, "odt") || !strcasecmp(ext, "ott") || !strcasecmp(ext, "ods") ||
      !strcasecmp(ext, "odp") || !strcasecmp(ext, "fodt") || !strcasecmp(ext, "fods")) {
    return extract_odf_text(path);
  }
  return NULL;
}
#endif

#if defined(HAVE_POPPLER_GLIB) && defined(HAVE_TESSERACT)
typedef struct {
  unsigned char *data;
  size_t length;
  size_t capacity;
} CairoBuffer;

static cairo_status_t cairo_buffer_write(void *closure, const unsigned char *data, unsigned int length) {
  CairoBuffer *buffer = (CairoBuffer *) closure;
  if (!buffer || !data || length == 0) {
    return CAIRO_STATUS_SUCCESS;
  }
  size_t required = buffer->length + (size_t) length;
  if (required > buffer->capacity) {
    size_t new_cap = buffer->capacity ? buffer->capacity * 2 : 8192;
    while (new_cap < required) {
      new_cap *= 2;
    }
    unsigned char *next = realloc(buffer->data, new_cap);
    if (!next) {
      return CAIRO_STATUS_WRITE_ERROR;
    }
    buffer->data = next;
    buffer->capacity = new_cap;
  }
  memcpy(buffer->data + buffer->length, data, length);
  buffer->length += (size_t) length;
  return CAIRO_STATUS_SUCCESS;
}

static Pix *render_pdf_page_to_pix(PopplerPage *page) {
  if (!page) {
    return NULL;
  }
  double width_pt = 0.0;
  double height_pt = 0.0;
  poppler_page_get_size(page, &width_pt, &height_pt);
  if (width_pt <= 0 || height_pt <= 0) {
    return NULL;
  }
  const double dpi = 200.0;
  const double scale = dpi / 72.0;
  const int width_px = (int) ceil(width_pt * scale);
  const int height_px = (int) ceil(height_pt * scale);
  if (width_px <= 0 || height_px <= 0 || width_px > 20000 || height_px > 20000) {
    return NULL;
  }
  cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width_px, height_px);
  if (!surface) {
    return NULL;
  }
  cairo_t *cr = cairo_create(surface);
  if (!cr) {
    cairo_surface_destroy(surface);
    return NULL;
  }
  cairo_scale(cr, scale, scale);
  cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
  cairo_paint(cr);
  poppler_page_render(page, cr);
  cairo_destroy(cr);
  cairo_surface_flush(surface);

  CairoBuffer buffer = {0};
  cairo_status_t status = cairo_surface_write_to_png_stream(surface, cairo_buffer_write, &buffer);
  cairo_surface_destroy(surface);
  if (status != CAIRO_STATUS_SUCCESS || buffer.length == 0 || !buffer.data) {
    free(buffer.data);
    return NULL;
  }
  Pix *pix = pixReadMemPng(buffer.data, buffer.length);
  free(buffer.data);
  return pix;
}

static char *extract_pdf_text_ocr(const char *path) {
  if (!path) {
    return NULL;
  }
  GError *error = NULL;
  char *uri = g_filename_to_uri(path, NULL, &error);
  if (!uri) {
    if (error) {
      g_error_free(error);
    }
    return NULL;
  }
  PopplerDocument *doc = poppler_document_new_from_file(uri, NULL, &error);
  g_free(uri);
  if (!doc) {
    if (error) {
      g_error_free(error);
    }
    return NULL;
  }
  TessBaseAPI *api = TessBaseAPICreate();
  if (!api) {
    g_object_unref(doc);
    return NULL;
  }
  const char *lang = getenv("TESSERACT_LANG");
  if (!lang || !*lang) {
    lang = "eng";
  }
  if (TessBaseAPIInit3(api, NULL, lang) != 0) {
    TessBaseAPIDelete(api);
    g_object_unref(doc);
    return NULL;
  }
  TessBaseAPISetPageSegMode(api, PSM_AUTO);

  int pages = poppler_document_get_n_pages(doc);
  StringBuffer sb;
  sb_init(&sb);
  for (int i = 0; i < pages; ++i) {
    PopplerPage *page = poppler_document_get_page(doc, i);
    if (!page) {
      continue;
    }
    Pix *pix = render_pdf_page_to_pix(page);
    g_object_unref(page);
    if (!pix) {
      continue;
    }
    TessBaseAPISetImage2(api, pix);
    char *text = TessBaseAPIGetUTF8Text(api);
    if (text && *text) {
      if (pages > 1) {
        sb_append_printf(&sb, "----- Page %d -----\n", i + 1);
      }
      sb_append_str(&sb, text);
      sb_append_char(&sb, '\n');
    }
    if (text) {
      TessDeleteText(text);
    }
    pixDestroy(&pix);
  }
  TessBaseAPIDelete(api);
  g_object_unref(doc);
  if (sb.length == 0) {
    sb_clean(&sb);
    return NULL;
  }
  return sb_detach(&sb);
}
#endif

static int format_binary_payload(const char *path, const char *mime, const unsigned char *data, size_t len,
                                 AttachmentResult *result) {
  char *encoded = base64_encode(data, len);
  if (!encoded) {
    return -1;
  }
  StringBuffer sb;
  sb_init(&sb);
  sb_append_printf(&sb, "Attachment %s (%s, %zu bytes) base64:\n", path, mime ? mime : "application/octet-stream",
                   len);
  sb_append_str(&sb, encoded);
  sb_append_char(&sb, '\n');
  free(encoded);
  result->message_text = sb_detach(&sb);
  result->mime_label = mime ? strdup(mime) : strdup("application/octet-stream");
  result->is_textual = 0;
  return 0;
}

static int format_text_payload(const char *path, const char *mime, const char *text, size_t len,
                               AttachmentResult *result) {
  size_t limit = len > 65536 ? 65536 : len;
  StringBuffer sb;
  sb_init(&sb);
  sb_append_printf(&sb, "Attachment %s (%s, %zu bytes)\n", path, mime ? mime : "text/plain", len);
  sb_append(&sb, text, limit);
  if (limit < len) {
    sb_append_str(&sb, "\n... [truncated]\n");
  }
  result->message_text = sb_detach(&sb);
  result->mime_label = mime ? strdup(mime) : strdup("text/plain");
  result->is_textual = 1;
  return 0;
}

int attachment_format_message(const char *path, AttachmentResult *result, char **error_out) {
  if (!result) {
    if (error_out) {
      *error_out = strdup("internal: result missing");
    }
    return -1;
  }
  memset(result, 0, sizeof *result);
  unsigned char *bytes = NULL;
  size_t len = 0;
  if (read_all_bytes(path, &bytes, &len, error_out) != 0) {
    return -1;
  }
  char *magic_err = NULL;
  const char *mime = detect_mime_type(path, bytes, len, &magic_err);
  if (magic_err) {
    free(magic_err);
  }
#if defined(HAVE_POPPLER_GLIB) || (defined(HAVE_LIBARCHIVE) && defined(HAVE_LIBXML2))
  const char *ext = extension_label(path);
#endif

#if defined(HAVE_POPPLER_GLIB) && defined(HAVE_TESSERACT)
  bool is_pdf = (mime && strstr(mime, "pdf"));
#if defined(HAVE_POPPLER_GLIB) || (defined(HAVE_LIBARCHIVE) && defined(HAVE_LIBXML2))
  if (!is_pdf && ext && !strcasecmp(ext, "pdf")) {
    is_pdf = true;
  }
#endif
  if (is_pdf) {
    char *ocr_text = extract_pdf_text_ocr(path);
    if (ocr_text) {
      int rc = format_text_payload(path, mime, ocr_text, strlen(ocr_text), result);
      free(ocr_text);
      free(bytes);
      free((void *) mime);
      return rc;
    }
  }
#endif

#if defined(HAVE_LIBARCHIVE) && defined(HAVE_LIBXML2)
  if (ext && (!strcasecmp(ext, "docx"))) {
    char *text = extract_docx_text(path);
    if (text) {
      int rc = format_text_payload(path, mime, text, strlen(text), result);
      free(text);
      free(bytes);
      free((void *) mime);
      return rc;
    }
  } else if (ext && (!strcasecmp(ext, "xlsx"))) {
    char *text = extract_xlsx_text(path);
    if (text) {
      int rc = format_text_payload(path, mime, text, strlen(text), result);
      free(text);
      free(bytes);
      free((void *) mime);
      return rc;
    }
  }
#endif

  DataClass cls = classify_buffer(bytes, len);
  if (mime_is_textual(mime) || cls == DATA_CLASS_TEXT) {
    int rc = format_text_payload(path, mime, (const char *) bytes, len, result);
    free(bytes);
    free((void *) mime);
    return rc;
  }
  int rc = format_binary_payload(path, mime, bytes, len, result);
  free(bytes);
  free((void *) mime);
  if (rc != 0 && error_out) {
    *error_out = strdup("unable to encode attachment");
  }
  return rc;
}

void attachment_result_clean(AttachmentResult *result) {
  if (!result) {
    return;
  }
  free(result->message_text);
  free(result->mime_label);
  result->message_text = NULL;
  result->mime_label = NULL;
}

int attachment_extract_text_payload(const char *path, AttachmentTextPayload *payload, char **error_out) {
  if (!path || !payload) {
    assign_error(error_out, "internal: missing file or payload");
    return -1;
  }
  memset(payload, 0, sizeof *payload);
  unsigned char *bytes = NULL;
  size_t len = 0;
  if (read_all_bytes(path, &bytes, &len, error_out) != 0) {
    return -1;
  }
  int rc = -1;
  char *magic_err = NULL;
  char *mime = (char *) detect_mime_type(path, bytes, len, &magic_err);
  if (magic_err) {
    free(magic_err);
  }
  if (!mime) {
    mime = strdup("application/octet-stream");
    if (!mime) {
      assign_error(error_out, "unable to allocate mime label");
      goto fail;
    }
  }
  payload->mime_label = mime;
#if defined(HAVE_POPPLER_GLIB) || (defined(HAVE_LIBARCHIVE) && defined(HAVE_LIBXML2))
  const char *ext = extension_label(path);
#endif
#if defined(HAVE_POPPLER_GLIB) && defined(HAVE_TESSERACT)
  bool is_pdf = (payload->mime_label && strstr(payload->mime_label, "pdf"));
#if defined(HAVE_POPPLER_GLIB) || (defined(HAVE_LIBARCHIVE) && defined(HAVE_LIBXML2))
  if (!is_pdf && ext && !strcasecmp(ext, "pdf")) {
    is_pdf = true;
  }
#endif
  if (is_pdf) {
    char *ocr_text = extract_pdf_text_ocr(path);
    if (ocr_text) {
      payload->data = ocr_text;
      payload->length = strlen(ocr_text);
      payload->extracted_from_container = true;
      payload->is_textual = true;
      rc = 0;
      goto done;
    }
  }
#endif

#if defined(HAVE_LIBARCHIVE) && defined(HAVE_LIBXML2)
  char *extracted = extract_office_like_text(path, ext);
  if (extracted) {
    payload->data = extracted;
    payload->length = strlen(extracted);
    payload->extracted_from_container = true;
    payload->is_textual = true;
    rc = 0;
    goto done;
  }
#endif

  DataClass cls = classify_buffer(bytes, len);
  bool textual = (payload->mime_label && mime_is_textual(payload->mime_label)) || cls == DATA_CLASS_TEXT;
  if (textual) {
    char *copy = malloc(len + 1);
    if (!copy) {
      assign_error(error_out, "unable to allocate %zu bytes", len + 1);
      goto fail;
    }
    memcpy(copy, bytes, len);
    copy[len] = '\0';
    payload->data = copy;
    payload->length = len;
    payload->is_textual = true;
    rc = 0;
    goto done;
  }

  AttachmentResult bridge;
  memset(&bridge, 0, sizeof bridge);
  if (format_binary_payload(path, payload->mime_label, bytes, len, &bridge) != 0) {
    assign_error(error_out, "unable to encode binary file %s", path);
    goto fail;
  }
  payload->data = bridge.message_text;
  payload->length = bridge.message_text ? strlen(bridge.message_text) : 0;
  payload->is_textual = true;
  payload->encoded_binary = true;
  free(bridge.mime_label);
  rc = 0;

done:
  free(bytes);
  return rc;

fail:
  free(bytes);
  attachment_text_payload_clean(payload);
  return -1;
}

void attachment_text_payload_clean(AttachmentTextPayload *payload) {
  if (!payload) {
    return;
  }
  free(payload->data);
  free(payload->mime_label);
  payload->data = NULL;
  payload->mime_label = NULL;
  payload->length = 0;
  payload->extracted_from_container = false;
  payload->is_textual = false;
  payload->encoded_binary = false;
}
