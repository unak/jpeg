/*
 * Copyright (c) 2007  NAKAMURA Usaku <usa@garbagecollect.jp>
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

#include <stdio.h>
#include <jpeglib.h>
#include <jerror.h>
#undef HAVE_PROTOTYPES
#undef HAVE_STDLIB_H
#undef EXTERN
#include <ruby.h>
#include <rubyio.h>

#ifndef RSTRING_PTR
#define RSTRING_PTR(s) (RSTRING(s)->ptr)
#endif
#ifndef RSTRING_LEN
#define RSTRING_LEN(s) (RSTRING(s)->len)
#endif


#define MY_VERSION "0.1"
#define SRCBITS 24

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
static VALUE cReader;
static VALUE cWriter;


static void
jp_error_exit(j_common_ptr jcp)
{
    jpeg_abort(jcp);
    if (jcp->err->msg_code != JERR_TOO_LITTLE_DATA) {
	rb_raise(eJpegError, "jpeg error");
    }
}

static VALUE
im_initialize(VALUE self)
{
    rb_iv_set(self, "raw_data", rb_str_new(NULL, 0));
    rb_iv_set(self, "width", INT2FIX(0));
    rb_iv_set(self, "height", INT2FIX(0));
    rb_iv_set(self, "quality", INT2FIX(0));

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

    dinfo.output_components = 3;
    dinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&dinfo);
    size = dinfo.image_width * SRCBITS / 8;
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

static VALUE
jp_s_write(VALUE klass, VALUE obj, VALUE dest)
{
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    FILE *fp;
    long size;
    long offset;
    char *line;
    VALUE raw_data;
    long width, height;
    int quality;

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
    if (width <= 0 || height <= 0 || quality <= 0 || quality > 100) {
	rb_raise(eJpegError, "illegal internal paramter");
    }
    raw_data = rb_iv_get(obj, "raw_data");
    if (RSTRING_LEN(raw_data) < width * 3 * height) {
	rb_raise(eJpegError, "raw_data is smaller than width and height");
    }

    cinfo.err = jpeg_std_error(&jerr);
    jerr.error_exit = jp_error_exit;
    jpeg_create_compress(&cinfo);
    jpeg_stdio_dest(&cinfo, fp);

    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    cinfo.progressive_mode = 1;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, 0);
    cinfo.optimize_coding = 1;
    cinfo.dct_method = JDCT_ISLOW;
    jpeg_start_compress(&cinfo, 1);

    size = width * SRCBITS / 8;
    offset = 0;
    while (cinfo.next_scanline < height) {
	JSAMPROW work = (JSAMPROW)&RSTRING_PTR(raw_data)[offset];
	jpeg_write_scanlines(&cinfo, (JSAMPARRAY)&work , 1);
	offset += size;
    }

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);

    return obj;
}

static void
get_point(char *ptr, long width, double x, double y, int *r, int *g, int *b)
{
    long x1, y1;
    double dx, dy;
    char c0[3], c1[3], c2[3], c3[3];

    x1 = (long)x;
    y1 = (long)y;
    dx = x - x1;
    dy = y - y1;

    c0[0] = ptr[(x1 + 0) * 3 + (y1 + 0) * 3 * width + 0];
    c0[1] = ptr[(x1 + 0) * 3 + (y1 + 0) * 3 * width + 1];
    c0[2] = ptr[(x1 + 0) * 3 + (y1 + 0) * 3 * width + 2];

    c1[0] = ptr[(x1 + 1) * 3 + (y1 + 0) * 3 * width + 0];
    c1[1] = ptr[(x1 + 1) * 3 + (y1 + 0) * 3 * width + 1];
    c1[2] = ptr[(x1 + 1) * 3 + (y1 + 0) * 3 * width + 2];

    c2[0] = ptr[(x1 + 0) * 3 + (y1 + 1) * 3 * width + 0];
    c2[1] = ptr[(x1 + 0) * 3 + (y1 + 1) * 3 * width + 1];
    c2[2] = ptr[(x1 + 0) * 3 + (y1 + 1) * 3 * width + 2];

    c3[0] = ptr[(x1 + 1) * 3 + (y1 + 1) * 3 * width + 0];
    c3[1] = ptr[(x1 + 1) * 3 + (y1 + 1) * 3 * width + 1];
    c3[2] = ptr[(x1 + 1) * 3 + (y1 + 1) * 3 * width + 2];

    *r = dx * dy * (c0[0] - c1[0] - c2[0] + c3[0]) + dx * (c1[0] - c0[0]) + dy * (c2[0] - c0[0]) + c0[0];
    *g = dx * dy * (c0[1] - c1[1] - c2[1] + c3[1]) + dx * (c1[1] - c0[1]) + dy * (c2[1] - c0[1]) + c0[1];
    *b = dx * dy * (c0[2] - c1[2] - c2[2] + c3[2]) + dx * (c1[2] - c0[2]) + dy * (c2[2] - c0[2]) + c0[2];
}

static VALUE
im_bilinear(VALUE self, VALUE dwidth, VALUE dheight)
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

    dw = NUM2LONG(dwidth);
    dh = NUM2LONG(dheight);
    width = NUM2LONG(rb_iv_get(self, "width"));
    height = NUM2LONG(rb_iv_get(self, "height"));
    src = rb_iv_get(self, "raw_data");
    dest = rb_str_new(NULL, 0);
    rb_str_resize(dest, dw * dh * 3);
    bx = (double)width / dw;
    by = (double)height / dh;
    for (y1 = y2 = 0; y1 < dh; y1++) {
	for (x1 = x2 = 0; x1 < dw; x1++) {
	    int r, g, b;
	    get_point(RSTRING_PTR(src), width, x2, y2, &r, &g, &b);
	    RSTRING_PTR(dest)[x1 * 3 + y1 * dw * 3 + 0] = r;
	    RSTRING_PTR(dest)[x1 * 3 + y1 * dw * 3 + 1] = g;
	    RSTRING_PTR(dest)[x1 * 3 + y1 * dw * 3 + 2] = b;
	    x2 += bx;
	}
	y2 += by;
    }

    jpeg = rb_class_new_instance(0, 0, cImage);
    rb_iv_set(jpeg, "raw_data", dest);
    rb_iv_set(jpeg, "width", LONG2NUM(dw));
    rb_iv_set(jpeg, "height", LONG2NUM(dh));
    rb_iv_set(jpeg, "quality", INT2FIX(100));

    return jpeg;
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

    size = rdp->dinfo.image_width * SRCBITS / 8;
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
    VALUE width, height, quality;
    struct writer_st *wrp;
    FILE *fp;

    rb_scan_args(argc, argv, "40", &dest, &width, &height, &quality);
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
    wrp->cinfo.input_components = 3;
    wrp->cinfo.in_color_space = JCS_RGB;
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

    size = wrp->cinfo.image_width * SRCBITS / 8;
    while (wrp->cinfo.next_scanline < wrp->cinfo.image_height) {
	JSAMPROW work;
	VALUE line = rb_yield(Qundef);
	StringValue(line);
	if (RSTRING_LEN(line) < size) {
	    rb_raise(eJpegError, "too short data passed");
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
	rb_define_class_under(mJpeg, "InternalError", rb_eStandardError);
}
