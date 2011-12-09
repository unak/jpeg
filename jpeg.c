/*
 * Copyright (c) 2007,2011  NAKAMURA Usaku <usa@garbagecollect.jp>
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <ruby.h>
#include <rubyio.h>
#include <st.h>

#include <stdio.h>

#undef HAVE_PROTOTYPES
#undef HAVE_STDDEF_H
#undef HAVE_STDLIB_H
#undef EXTERN
#include <jpeglib.h>
#include <jerror.h>

#ifndef RSTRING_PTR
#define RSTRING_PTR(s) (RSTRING(s)->ptr)
#endif
#ifndef RSTRING_LEN
#define RSTRING_LEN(s) (RSTRING(s)->len)
#endif

#if HAVE_RB_IO_T
#define OpenFile rb_io_t
#endif

#define MY_VERSION "0.4"


#define define_accessor(klass, pref, val)	\
static VALUE					\
pref##_get_##val(VALUE self)			\
{						\
    return rb_iv_get(self, #val);		\
}						\
static VALUE					\
pref##_set_##val(VALUE self, VALUE v)		\
{						\
    rb_iv_set(self, #val, v);			\
    return self;				\
}
#define register_accessor(klass, pref, val)		\
    rb_define_method(klass, #val, pref##_get_##val, 0);	\
    rb_define_method(klass, #val "=", pref##_set_##val, 1)


static VALUE mJpeg;
static VALUE cImage;
static VALUE eJpegError;
static VALUE eJpegUnknownError;
static VALUE cReader;
static VALUE cWriter;

static st_table *jp_err_tbl;


static void
jp_error_exit(j_common_ptr jcp)
{
    VALUE err;

    jpeg_abort(jcp);
    if (jcp->err->msg_code >= 0 &&
	jcp->err->msg_code <= jcp->err->last_jpeg_message) {
	if (!st_lookup(jp_err_tbl, jcp->err->msg_code, &err)) {
	    err = eJpegUnknownError;
	}
	rb_raise(err, jcp->err->jpeg_message_table[jcp->err->msg_code],
		 jcp->err->msg_parm.i[0], jcp->err->msg_parm.i[1],
		 jcp->err->msg_parm.i[2], jcp->err->msg_parm.i[3],
		 jcp->err->msg_parm.i[4], jcp->err->msg_parm.i[5],
		 jcp->err->msg_parm.i[6], jcp->err->msg_parm.i[7]);
    }
    else {
	rb_raise(eJpegUnknownError, "unknown internal error");
    }
}

static VALUE
im_initialize(VALUE self)
{
    rb_iv_set(self, "raw_data", rb_str_new(NULL, 0));
    rb_iv_set(self, "width", INT2FIX(0));
    rb_iv_set(self, "height", INT2FIX(0));
    rb_iv_set(self, "quality", INT2FIX(0));
    rb_iv_set(self, "gray_p", Qfalse);

    return self;
}

static VALUE
jp_s_read(VALUE klass, VALUE src)
{
    struct jpeg_decompress_struct dinfo;
    struct jpeg_error_mgr jerr;
    FILE *fp;
    long size;
    long offset;
    char *line;
    long len;
    VALUE obj;
    VALUE raw_data;

    if (TYPE(src) == T_FILE) {
	OpenFile *fptr;
	rb_io_binmode(src);
	GetOpenFile(src, fptr);
#ifdef GetReadFile
	fp = GetReadFile(fptr);
#else
	fp = rb_io_stdio_file(fptr);
#endif
    }
    else {
	rb_raise(rb_eTypeError, "need IO");
    }

    dinfo.err = jpeg_std_error(&jerr);
    jerr.error_exit = jp_error_exit;
    jpeg_create_decompress(&dinfo);
    jpeg_stdio_src(&dinfo, fp);

    jpeg_read_header(&dinfo, 1);
    obj = rb_obj_alloc(cImage);
    rb_obj_call_init(obj, 0, NULL);
    rb_iv_set(obj, "width", LONG2NUM(dinfo.image_width));
    rb_iv_set(obj, "height", LONG2NUM(dinfo.image_height));
    rb_iv_set(obj, "quality", INT2FIX(100));	/* always 100 */
    rb_iv_set(obj, "gray_p", dinfo.out_color_space == JCS_GRAYSCALE ? Qtrue : Qfalse);

    if (dinfo.out_color_space == JCS_GRAYSCALE) {
	dinfo.output_components = 1;
    }
    else {
	dinfo.output_components = 3;
	dinfo.out_color_space = JCS_RGB;
    }
    jpeg_start_decompress(&dinfo);
    size = dinfo.image_width * dinfo.output_components;
    len = size * dinfo.image_height;
    raw_data = rb_iv_get(obj, "raw_data");
    rb_str_resize(raw_data, len);
    offset = 0;
    while (dinfo.output_scanline < dinfo.image_height) {
	JSAMPROW work = (JSAMPROW)&RSTRING_PTR(raw_data)[offset];
	jpeg_read_scanlines(&dinfo, (JSAMPARRAY)&work , 1);
	offset += size;
    }

    jpeg_finish_decompress(&dinfo);
    jpeg_destroy_decompress(&dinfo);

    return obj;
}

static inline unsigned char
grayscale(unsigned char r, unsigned char g, unsigned char b)
{
    return (unsigned char)((r * 77 + g * 150 + b * 29) >> 8);
}

static VALUE
jp_s_write(VALUE klass, VALUE obj, VALUE dest)
{
    VALUE gray;
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    FILE *fp;
    long size;
    long offset;
    char *line;
    VALUE raw_data, buffer;
    long width, height;
    int quality;
    JSAMPROW work;

    if (TYPE(dest) == T_FILE) {
	OpenFile *fptr;
	rb_io_binmode(dest);
	GetOpenFile(dest, fptr);
#ifdef GetReadFile
	fp = GetWriteFile(fptr);
#else
	fp = rb_io_stdio_file(fptr);
#endif
    }
    else {
	rb_raise(rb_eTypeError, "need IO");
    }

    width = NUM2LONG(rb_iv_get(obj, "width"));
    height = NUM2LONG(rb_iv_get(obj, "height"));
    quality = FIX2INT(rb_iv_get(obj, "quality"));
    gray = rb_iv_get(obj, "gray_p");
    if (width <= 0 || height <= 0 || quality <= 0 || quality > 100) {
	rb_raise(rb_eArgError, "invalid internal paramter");
    }
    raw_data = rb_iv_get(obj, "raw_data");
    if (RSTRING_LEN(raw_data) < width * height * (RTEST(gray) ? 1 : 3)) {
	rb_raise(rb_eArgError, "raw_data is smaller than width and height");
    }

    cinfo.err = jpeg_std_error(&jerr);
    jerr.error_exit = jp_error_exit;
    jpeg_create_compress(&cinfo);
    jpeg_stdio_dest(&cinfo, fp);

    cinfo.image_width = width;
    cinfo.image_height = height;
    if (RTEST(gray)) {
	cinfo.input_components = 1;
	cinfo.in_color_space = JCS_GRAYSCALE;
    }
    else {
	cinfo.input_components = 3;
	cinfo.in_color_space = JCS_RGB;
    }
    cinfo.progressive_mode = 1;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, 0);
    cinfo.optimize_coding = 1;
    cinfo.dct_method = JDCT_ISLOW;
    jpeg_start_compress(&cinfo, 1);

    size = width * cinfo.input_components;
    offset = 0;
    while (cinfo.next_scanline < height) {
	work = (JSAMPROW)&RSTRING_PTR(raw_data)[offset];
	jpeg_write_scanlines(&cinfo, (JSAMPARRAY)&work , 1);
	offset += size;
    }

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);

    return obj;
}

static void
get_point_bilinear(unsigned char *ptr, long width, long height, int components, double x, double y, int *r, int *g, int *b)
{
    long x1, y1;
    double dx, dy;
    unsigned char c[3][4];
    int i;

    x1 = (long)x;
    y1 = (long)y;
    dx = x - x1;
    dy = y - y1;

    for (i = 0; i < components; ++i) {
	c[0][i] = ptr[((x1 + 0) + (y1 + 0) * width) * components + i];
	c[1][i] = ptr[((x1 + 1) + (y1 + 0) * width) * components + i];
	c[2][i] = ptr[((x1 + 0) + (y1 + 1) * width) * components + i];
	c[3][i] = ptr[((x1 + 1) + (y1 + 1) * width) * components + i];
    }

    *r = (int)(dx * dy * (c[0][0] - c[1][0] - c[2][0] + c[3][0]) + dx * (c[1][0] - c[0][0]) + dy * (c[2][0] - c[0][0]) + c[0][0]);
    if (components > 1) {
	*g = (int)(dx * dy * (c[0][1] - c[1][1] - c[2][1] + c[3][1]) + dx * (c[1][1] - c[0][1]) + dy * (c[2][1] - c[0][1]) + c[0][1]);
	*b = (int)(dx * dy * (c[0][2] - c[1][2] - c[2][2] + c[3][2]) + dx * (c[1][2] - c[0][2]) + dy * (c[2][2] - c[0][2]) + c[0][2]);
    }
}

static inline double
bicubic_weight(double d)
{
    double d2 = d * d;
    double d3 = d2 * d;
    return d < 1.0 ? (1.0 - 2.0 * d2 + d3) : d < 2.0 ? (4.0 - 8.0 * d + 5.0 * d2 - d3) : 0.0;
}

static inline int
saturate(int n, int min, int max)
{
    return n < min ? min : n > max ? max : n;
}

static void
get_point_bicubic(unsigned char *ptr, long width, long height, int components, double dx, double dy, int *pr, int *pg, int *pb)
{
    long x[4], y[4];
    long wx[4], wy[4], wt;
    long r, g, b;
    int i, j, k;

    x[1] = (long)dx;
    x[0] = x[1] - 1;
    x[2] = x[1] + 1;
    x[3] = x[2] + 1;

    y[1] = (long)dy;
    y[0] = y[1] - 1;
    y[2] = y[1] + 1;
    y[3] = y[2] + 1;

    wx[0] = (long)(bicubic_weight(dx - x[0]) * 1024);
    wx[1] = (long)(bicubic_weight(dx - x[1]) * 1024);
    wx[2] = (long)(bicubic_weight(x[2] - dx) * 1024);
    wx[3] = (long)(bicubic_weight(x[3] - dx) * 1024);
    wy[0] = (long)(bicubic_weight(dy - y[0]) * 1024);
    wy[1] = (long)(bicubic_weight(dy - y[1]) * 1024);
    wy[2] = (long)(bicubic_weight(y[2] - dy) * 1024);
    wy[3] = (long)(bicubic_weight(y[3] - dy) * 1024);

    r = g = b = 0;
    wt = 0;
    for (j = 0; j < 4; ++j) {
	if (y[j] >= 0 && y[j] < height) {
	    for (i = 0; i < 4; ++i) {
		if (x[i] >= 0 && x[i] < width) {
		    long w = wx[i] * wy[j];
		    int pos = (x[i] + y[j] * width) * components;
		    r += ptr[pos + 0] * w;
		    if (components > 1) {
			g += ptr[pos + 1] * w;
			b += ptr[pos + 2] * w;
		    }
		    wt += w;
		}
	    }
	}
    }

    *pr = saturate((int)(r / wt), 0, 255);
    if (components > 1) {
	*pg = saturate((int)(g / wt), 0, 255);
	*pb = saturate((int)(b / wt), 0, 255);
    }
}

typedef void (* get_point_t)(unsigned char *, long, long, int, double, double, int *, int *, int *);

static VALUE
im_resize(get_point_t get_point, VALUE self, VALUE dwidth, VALUE dheight)
{
    long width, height;
    long dw, dh;
    double bx;
    double by;
    long x1, y1;
    double x2, y2;
    VALUE src;
    VALUE dest;
    VALUE jpeg;
    int components;

    dw = NUM2LONG(dwidth);
    dh = NUM2LONG(dheight);
    width = NUM2LONG(rb_iv_get(self, "width"));
    height = NUM2LONG(rb_iv_get(self, "height"));
    src = rb_iv_get(self, "raw_data");
    components = RTEST(rb_iv_get(self, "gray_p")) ? 1 : 3;
    dest = rb_str_new(NULL, 0);
    rb_str_resize(dest, dw * dh * components);
    bx = (double)width / dw;
    by = (double)height / dh;
    for (y1 = 0, y2 = 0.0; y1 < dh; y1++) {
	for (x1 = 0, x2 = 0.0; x1 < dw; x1++) {
	    int r, g, b;
	    get_point(RSTRING_PTR(src), width, height, components, x2, y2, &r, &g, &b);
	    RSTRING_PTR(dest)[(x1 + y1 * dw) * components + 0] = r;
	    if (components > 1) {
		RSTRING_PTR(dest)[(x1 + y1 * dw) * components + 1] = g;
		RSTRING_PTR(dest)[(x1 + y1 * dw) * components + 2] = b;
	    }
	    x2 += bx;
	}
	y2 += by;
    }

    jpeg = rb_class_new_instance(0, 0, cImage);
    rb_iv_set(jpeg, "raw_data", dest);
    rb_iv_set(jpeg, "width", LONG2NUM(dw));
    rb_iv_set(jpeg, "height", LONG2NUM(dh));
    rb_iv_set(jpeg, "quality", INT2FIX(100));
    rb_iv_set(jpeg, "gray_p", rb_iv_get(self, "gray_p"));

    return jpeg;
}

static VALUE
im_bilinear(VALUE self, VALUE dwidth, VALUE dheight)
{
    return im_resize(get_point_bilinear, self, dwidth, dheight);
}

static VALUE
im_bicubic(VALUE self, VALUE dwidth, VALUE dheight)
{
    return im_resize(get_point_bicubic, self, dwidth, dheight);
}

static VALUE
im_contrast(VALUE self)
{
    VALUE jpeg;
    long width;
    long height;
    VALUE src;
    VALUE dest;
    long x, y;
    unsigned char min, max, median;
    int components;
    int low, high;
    int i;
    long sum, half, hist[256];

    width = NUM2LONG(rb_iv_get(self, "width"));
    height = NUM2LONG(rb_iv_get(self, "height"));
    src = rb_iv_get(self, "raw_data");
    components = RTEST(rb_iv_get(self, "gray_p")) ? 1 : 3;
    dest = rb_str_new(NULL, 0);
    rb_str_resize(dest, width * height * components);

    memset(hist, 0, sizeof(hist));
    min = 255; max = 0;
    for (y = 0; y < height; ++y) {
	for (x = 0; x < width; ++x) {
	    unsigned char *p = &RSTRING_PTR(src)[(x + y * width) * components];
	    unsigned char gray = components > 1 ? grayscale(p[0], p[1], p[2]) : *p;
	    min = min(gray, min);
	    max = max(gray, max);
	    hist[gray]++;
	}
    }

    half = width * height / 2;
    median = 128;
    sum = 0;
    for (i = 0; i < sizeof(hist); ++i) {
	sum += hist[i];
	if (sum >= half) {
	    median = i;
	    break;
	}
    }

    low = median - min;
    high = max - median;
    if (low && high) {
	for (y = 0; y < height; ++y) {
	    for (x = 0; x < width; ++x) {
		unsigned char *p = &RSTRING_PTR(src)[(x + y * width) * components];
		unsigned char *q = &RSTRING_PTR(dest)[(x + y * width) * components];
		int i;
		for (i = 0; i < components; ++i) {
		    q[i] = p[i] < median ? (p[i] - min) * 127 / low : (p[i] - median) * 127 / high + 128;
		}
	    }
	}
    }
    else {
	memcpy(RSTRING_PTR(dest), RSTRING_PTR(src), width * height * components);
    }

    jpeg = rb_class_new_instance(0, 0, cImage);
    rb_iv_set(jpeg, "raw_data", dest);
    rb_iv_set(jpeg, "width", LONG2NUM(width));
    rb_iv_set(jpeg, "height", LONG2NUM(height));
    rb_iv_set(jpeg, "quality", INT2FIX(100));
    rb_iv_set(jpeg, "gray_p", rb_iv_get(self, "gray_p"));

    return jpeg;
}

static VALUE
im_grayscale(VALUE self)
{
    VALUE jpeg;
    long width;
    long height;
    VALUE src;
    VALUE dest;
    long x, y;

    width = NUM2LONG(rb_iv_get(self, "width"));
    height = NUM2LONG(rb_iv_get(self, "height"));
    src = rb_iv_get(self, "raw_data");
    dest = rb_str_new(NULL, 0);
    rb_str_resize(dest, width * height);
    if (RTEST(rb_iv_get(self, "gray_p"))) {
	memcpy(RSTRING_PTR(dest), RSTRING_PTR(src), width * height);
    }
    else {
	unsigned char *q = RSTRING_PTR(dest);
	for (y = 0; y < height; ++y) {
	    for (x = 0; x < width; ++x, ++q) {
		unsigned char *p = &RSTRING_PTR(src)[x * 3 + y * width * 3];
		*q = grayscale(p[0], p[1], p[2]);
	    }
	}
    }

    jpeg = rb_class_new_instance(0, 0, cImage);
    rb_iv_set(jpeg, "raw_data", dest);
    rb_iv_set(jpeg, "width", LONG2NUM(width));
    rb_iv_set(jpeg, "height", LONG2NUM(height));
    rb_iv_set(jpeg, "quality", INT2FIX(100));
    rb_iv_set(jpeg, "gray_p", Qtrue);

    return jpeg;
}

static VALUE
im_level(int argc, VALUE *argv, VALUE self)
{
    VALUE l, h, adj = Qfalse;
    int low, high, d;
    VALUE jpeg;
    long width, height;
    VALUE src, dest;
    long x, y;
    int components;

    rb_scan_args(argc, argv, "21", &l, &h, &adj);
    low = NUM2LONG(l);
    high = NUM2LONG(h);
    if (low < 0 || low > 100 || high < 0 || high > 100) {
	rb_raise(rb_eArgError, "level must be between 1 to 100");
    }
    if (low >= high) {
	rb_raise(rb_eArgError, "low must be less than high");
    }
    low = low * 256 / 100;
    high = high * 256 / 100;
    d = high - low;

    width = NUM2LONG(rb_iv_get(self, "width"));
    height = NUM2LONG(rb_iv_get(self, "height"));
    src = rb_iv_get(self, "raw_data");
    components = RTEST(rb_iv_get(self, "gray_p")) ? 1 : 3;
    dest = rb_str_new(NULL, 0);
    rb_str_resize(dest, width * height * components);

    for (y = 0; y < height; ++y) {
	for (x = 0; x < width; ++x) {
	    unsigned char *p = &RSTRING_PTR(src)[(x + y * width) * components];
	    unsigned char *q = &RSTRING_PTR(dest)[(x + y * width) * components];
	    int i;
	    for (i = 0; i < components; ++i) {
		q[i] = p[i] < low ? 0 : p[i] >= high ? 255 : RTEST(adj) ? (p[i] - low) * d / 256 : p[i];
	    }
	}
    }

    jpeg = rb_class_new_instance(0, 0, cImage);
    rb_iv_set(jpeg, "raw_data", dest);
    rb_iv_set(jpeg, "width", LONG2NUM(width));
    rb_iv_set(jpeg, "height", LONG2NUM(height));
    rb_iv_set(jpeg, "quality", INT2FIX(100));
    rb_iv_set(jpeg, "gray_p", rb_iv_get(self, "gray_p"));

    return jpeg;
}

static VALUE
im_clip(int argc, VALUE *argv, VALUE self)
{
    long x1, y1, x2, y2;
    long width, height;
    long dwidth, dheight;
    long x, y;
    long i;
    int components;
    VALUE src, dest;
    VALUE jpeg;

    if (argc != 0 && argc != 4) {
	rb_raise(rb_eArgError,
		 "wrong number of arguments(%d for 0 or 4)", argc);
    }

    src = rb_iv_get(self, "raw_data");
    components = RTEST(rb_iv_get(self, "gray_p")) ? 1 : 3;

    if (argc == 0) {
	unsigned char base[3], *p;

	width = NUM2LONG(rb_iv_get(self, "width"));
	height = NUM2LONG(rb_iv_get(self, "height"));

	memcpy(base, RSTRING_PTR(src), components);

	x1 = width - 1;
	y1 = height - 1;
	for (y = 0; y < height; ++y) {
	    for (x = 0; x <= x1; ++x) {
		p = &RSTRING_PTR(src)[(x + y * width) * components];
		for (i = 0; i < components; ++i) {
		    if (p[i] != base[i]) {
			x1 = x;
			if (y < y1) y1 = y;
			break;
		    }
		}
	    }
	}

	x2 = x1;
	y2 = y1;
	for (y = height - 1; y >= 0; --y) {
	    for (x = width - 1; x >= x2; --x) {
		p = &RSTRING_PTR(src)[(x + y * width) * components];
		for (i = 0; i < components; ++i) {
		    if (p[i] != base[i]) {
			x2 = x;
			if (y > y2) y2 = y;
			break;
		    }
		}
	    }
	}

	if (x1 == x2 || y1 == y2) {
	    return Qnil;
	}
    }
    else {
	x1 = NUM2LONG(argv[0]);
	y1 = NUM2LONG(argv[1]);
	x2 = NUM2LONG(argv[2]);
	y2 = NUM2LONG(argv[3]);
	if (x1 >= x2 || y1 >= y2) {
	    rb_raise(rb_eArgError, "wrong combination of arguments");
	}
    }

    dwidth = x2 - x1 + 1;
    dheight = y2 - y1 + 1;
    dest = rb_str_new(NULL, 0);
    rb_str_resize(dest, dwidth * dheight * components);

    for (y = y1; y <= y2; ++y) {
	unsigned char *p = &RSTRING_PTR(src)[(x1 + y * width) * components];
	unsigned char *q = &RSTRING_PTR(dest)[(y - y1) * dwidth * components];
	memcpy(q, p, dwidth * components);
    }

    jpeg = rb_class_new_instance(0, 0, cImage);
    rb_iv_set(jpeg, "raw_data", dest);
    rb_iv_set(jpeg, "width", LONG2NUM(dwidth));
    rb_iv_set(jpeg, "height", LONG2NUM(dheight));
    rb_iv_set(jpeg, "quality", INT2FIX(100));
    rb_iv_set(jpeg, "gray_p", rb_iv_get(self, "gray_p"));

    return jpeg;
}

static VALUE
im_gray_p(VALUE self)
{
    return rb_iv_get(self, "gray_p");
}

define_accessor(cImage, im, raw_data);
define_accessor(cImage, im, width);
define_accessor(cImage, im, height);
define_accessor(cImage, im, quality);

struct reader_st {
    struct jpeg_decompress_struct dinfo;
    struct jpeg_error_mgr jerr;
    int open;
    long width;
    long height;
};

static VALUE
rd_close(VALUE self)
{
    struct reader_st *rdp;

    Data_Get_Struct(self, struct reader_st, rdp);
    if (rdp->open > 1) {
	rdp->open--;
	jpeg_finish_decompress(&rdp->dinfo);
    }
    if (rdp->open > 0) {
	rdp->open--;
	jpeg_destroy_decompress(&rdp->dinfo);
    }

    return Qnil;
}

static VALUE
rd_s_open(VALUE klass, VALUE src)
{
    VALUE obj;

    obj = rb_obj_alloc(klass);
    rb_obj_call_init(obj, 1, &src);

    if (rb_block_given_p()) {
        rb_ensure(rb_yield, obj, rd_close, obj);
	return Qnil;
    }
    else {
	return obj;
    }
}

static void
rd_free(struct reader_st *rdp)
{
    if (rdp) {
	if (rdp->open > 1) {
	    rdp->open--;
	    jpeg_finish_decompress(&rdp->dinfo);
	}
	if (rdp->open > 0) {
	    rdp->open--;
	    jpeg_destroy_decompress(&rdp->dinfo);
	}
	free(rdp);
    }
}

static VALUE
rd_alloc(VALUE klass)
{
    return Data_Wrap_Struct(klass, 0, rd_free, 0);
}

static VALUE
rd_initialize(VALUE self, VALUE src)
{
    struct reader_st *rdp;
    FILE *fp;

    if (TYPE(src) == T_FILE) {
	OpenFile *fptr;
	rb_io_binmode(src);
	GetOpenFile(src, fptr);
#ifdef GetReadFile
	fp = GetReadFile(fptr);
#else
	fp = rb_io_stdio_file(fptr);
#endif
    }
    else {
	rb_raise(rb_eTypeError, "need IO");
    }

    rdp = ALLOC(struct reader_st);
    rdp->open = 0;
    DATA_PTR(self) = rdp;

    rdp->dinfo.err = jpeg_std_error(&rdp->jerr);
    rdp->jerr.error_exit = jp_error_exit;
    jpeg_create_decompress(&rdp->dinfo);
    rdp->open++;
    jpeg_stdio_src(&rdp->dinfo, fp);

    jpeg_read_header(&rdp->dinfo, 1);
    rdp->width = rdp->dinfo.image_width;
    rdp->height = rdp->dinfo.image_height;

    rdp->dinfo.output_components = 3;
    rdp->dinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&rdp->dinfo);
    rdp->open++;

    return self;
}

static VALUE
rd_each(VALUE self)
{
    struct reader_st *rdp;
    long size;
    char *buf;

    Data_Get_Struct(self, struct reader_st, rdp);
    if (rdp->open < 2) {
	rb_raise(eJpegError, "not opened");
    }

    size = rdp->dinfo.image_width * rdp->dinfo.output_components;
    buf = ALLOCA_N(char, size);
    while (rdp->dinfo.output_scanline < rdp->dinfo.image_height) {
	JSAMPROW work = (JSAMPROW)buf;
	jpeg_read_scanlines(&rdp->dinfo, (JSAMPARRAY)&work , 1);
	rb_yield(rb_str_new(buf, size));
    }

    return Qnil;
}

static VALUE
rd_get_width(VALUE self)
{
    struct reader_st *rdp;

    Data_Get_Struct(self, struct reader_st, rdp);
    if (rdp->open < 2) {
	rb_raise(eJpegError, "not opened");
    }

    return LONG2NUM(rdp->width);
}

static VALUE
rd_get_height(VALUE self)
{
    struct reader_st *rdp;

    Data_Get_Struct(self, struct reader_st, rdp);
    if (rdp->open < 2) {
	rb_raise(eJpegError, "not opened");
    }

    return LONG2NUM(rdp->height);
}

struct writer_st {
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    int open;
    long width;
    long height;
    int quality;
};

static VALUE
wr_close(VALUE self)
{
    struct writer_st *wrp;

    Data_Get_Struct(self, struct writer_st, wrp);
    if (wrp->open > 2) {
	wrp->open -= 2;
	jpeg_finish_compress(&wrp->cinfo);
    }
    if (wrp->open > 0) {
	wrp->open--;
	jpeg_destroy_compress(&wrp->cinfo);
    }

    return Qnil;
}

static VALUE
wr_s_open(int argc, VALUE *argv, VALUE klass)
{
    VALUE obj;

    obj = rb_obj_alloc(klass);
    rb_obj_call_init(obj, argc, argv);

    if (rb_block_given_p()) {
        rb_ensure(rb_yield, obj, wr_close, obj);
	return Qnil;
    }
    else {
	return obj;
    }
}

static void
wr_free(struct writer_st *wrp)
{
    if (wrp) {
	if (wrp->open > 2) {
	    wrp->open -= 2;
	    jpeg_finish_compress(&wrp->cinfo);
	}
	if (wrp->open > 0) {
	    wrp->open--;
	    jpeg_destroy_compress(&wrp->cinfo);
	}
	free(wrp);
    }
}

static VALUE
wr_alloc(VALUE klass)
{
    return Data_Wrap_Struct(klass, 0, wr_free, 0);
}

static VALUE
wr_initialize(int argc, VALUE *argv, VALUE self)
{
    VALUE dest;
    VALUE width, height, quality, gray = Qfalse;
    struct writer_st *wrp;
    FILE *fp;

    rb_scan_args(argc, argv, "41", &dest, &width, &height, &quality, &gray);
    if (TYPE(dest) == T_FILE) {
	OpenFile *fptr;
	rb_io_binmode(dest);
	GetOpenFile(dest, fptr);
#ifdef GetWriteFile
	fp = GetWriteFile(fptr);
#else
	fp = rb_io_stdio_file(fptr);
#endif
    }
    else {
	rb_raise(rb_eTypeError, "need IO");
    }
    if (NUM2LONG(width) <= 0) {
	rb_raise(rb_eArgError, "too small width");
    }
    if (NUM2LONG(height) <= 0) {
	rb_raise(rb_eArgError, "too small height");
    }
    if (FIX2INT(quality) < 0 || FIX2INT(quality) > 100) {
	rb_raise(rb_eArgError, "quality must be between 1 to 100");
    }

    wrp = ALLOC(struct writer_st);
    wrp->open = 0;
    wrp->width = NUM2LONG(width);
    wrp->height = NUM2LONG(height);
    wrp->quality = FIX2INT(quality);
    DATA_PTR(self) = wrp;

    wrp->cinfo.err = jpeg_std_error(&wrp->jerr);
    wrp->jerr.error_exit = jp_error_exit;
    jpeg_create_compress(&wrp->cinfo);
    wrp->open++;
    jpeg_stdio_dest(&wrp->cinfo, fp);

    wrp->cinfo.image_width = wrp->width;
    wrp->cinfo.image_height = wrp->height;
    if (RTEST(gray)) {
	wrp->cinfo.input_components = 1;
	wrp->cinfo.in_color_space = JCS_GRAYSCALE;
    }
    else {
	wrp->cinfo.input_components = 3;
	wrp->cinfo.in_color_space = JCS_RGB;
    }
    wrp->cinfo.progressive_mode = 1;
    jpeg_set_defaults(&wrp->cinfo);
    jpeg_set_quality(&wrp->cinfo, wrp->quality, 0);
    wrp->cinfo.optimize_coding = 1;
    wrp->cinfo.dct_method = JDCT_ISLOW;
    jpeg_start_compress(&wrp->cinfo, 1);
    wrp->open++;

    return self;
}

static VALUE
wr_each(VALUE self)
{
    struct writer_st *wrp;
    long size;

    Data_Get_Struct(self, struct writer_st, wrp);
    if (wrp->open < 2) {
	rb_raise(eJpegError, "not opened");
    }

    size = wrp->cinfo.image_width * wrp->cinfo.input_components;
    while (wrp->cinfo.next_scanline < wrp->cinfo.image_height) {
	JSAMPROW work;
	VALUE line = rb_yield(Qundef);
	StringValue(line);
	if (RSTRING_LEN(line) < size) {
	    rb_raise(rb_eArgError, "too short data passed");
	}
	work = (JSAMPROW)RSTRING_PTR(line);
	jpeg_write_scanlines(&wrp->cinfo, (JSAMPARRAY)&work , 1);
    }
    wrp->open++;

    return Qnil;
}

static VALUE
wr_get_width(VALUE self)
{
    struct writer_st *wrp;

    Data_Get_Struct(self, struct writer_st, wrp);
    if (wrp->open < 2) {
	rb_raise(eJpegError, "not opened");
    }

    return LONG2NUM(wrp->width);
}

static VALUE
wr_get_height(VALUE self)
{
    struct writer_st *wrp;

    Data_Get_Struct(self, struct writer_st, wrp);
    if (wrp->open < 2) {
	rb_raise(eJpegError, "not opened");
    }

    return LONG2NUM(wrp->height);
}

static VALUE
wr_get_quality(VALUE self)
{
    struct writer_st *wrp;

    Data_Get_Struct(self, struct writer_st, wrp);
    if (wrp->open < 2) {
	rb_raise(eJpegError, "not opened");
    }

    return INT2FIX(wrp->quality);
}

static VALUE
set_jp_err(int n, const char *name)
{
    VALUE err;

    err = rb_define_class_under(mJpeg, name, eJpegError);
    rb_define_const(err, "Errno", INT2NUM(n));
    st_add_direct(jp_err_tbl, n, err);

    return err;
}

void
Init_jpeg(void)
{
    mJpeg = rb_define_module("JPEG");
    rb_define_const(mJpeg, "VERSION", rb_str_new2(MY_VERSION));
    rb_define_singleton_method(mJpeg, "read", jp_s_read, 1);
    rb_define_singleton_method(mJpeg, "write", jp_s_write, 2);

    cImage = rb_define_class_under(mJpeg, "Image", rb_cObject);
    rb_define_method(cImage, "initialize", im_initialize, 0);
    rb_define_method(cImage, "bilinear", im_bilinear, 2);
    rb_define_method(cImage, "bicubic", im_bicubic, 2);
    rb_define_method(cImage, "auto_contrast", im_contrast, 0);
    rb_define_method(cImage, "grayscale", im_grayscale, 0);
    rb_define_method(cImage, "level", im_level, -1);
    rb_define_method(cImage, "clip", im_clip, -1);
    rb_define_method(cImage, "gray?", im_gray_p, 0);
    register_accessor(cImage, im, raw_data);
    register_accessor(cImage, im, width);
    register_accessor(cImage, im, height);
    register_accessor(cImage, im, quality);

    cReader = rb_define_class_under(mJpeg, "Reader", rb_cObject);
    rb_define_singleton_method(cReader, "open", rd_s_open, 1);
    rb_define_alloc_func(cReader, rd_alloc);
    rb_define_method(cReader, "initialize", rd_initialize, 1);
    rb_define_method(cReader, "close", rd_close, 0);
    rb_define_method(cReader, "each", rd_each, 0);
    rb_define_method(cReader, "each_line", rd_each, 0);
    rb_define_method(cReader, "read_each_line", rd_each, 0);
    rb_define_method(cReader, "width", rd_get_width, 0);
    rb_define_method(cReader, "height", rd_get_height, 0);

    cWriter = rb_define_class_under(mJpeg, "Writer", rb_cObject);
    rb_define_singleton_method(cWriter, "open", wr_s_open, -1);
    rb_define_alloc_func(cWriter, wr_alloc);
    rb_define_method(cWriter, "initialize", wr_initialize, -1);
    rb_define_method(cWriter, "close", wr_close, 0);
    rb_define_method(cWriter, "write_each_line", wr_each, 0);
    rb_define_method(cWriter, "width", wr_get_width, 0);
    rb_define_method(cWriter, "height", wr_get_height, 0);
    rb_define_method(cWriter, "quality", wr_get_quality, 0);

    eJpegError =
	rb_define_class_under(mJpeg, "StandardError", rb_eStandardError);
    jp_err_tbl = st_init_numtable();
#define JMESSAGE(code,string)	set_jp_err(code, #code);
#include "jerror.h"
    eJpegUnknownError =
	rb_define_class_under(mJpeg, "UnknownError", eJpegError);
}
