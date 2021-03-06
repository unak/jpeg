# Simple JPEG Extension Library for Ruby

## Overview

This is a simple JPEG extension library for Ruby.
This library only supports reading and writing JPEG files.
You can access raw RGB data if you need.


## Requires

IJG's jpeg library (libjpeg)


## How to build

 $ ruby extconf.rb
 $ make

If you use VC++, run nmake instead of make.

If extconf.rb cannot find libjpeg headers and/or library, you should specify
their paths by --with-jpeg-include and --with-jpeg-lib options.


## Reference

### module `JPEG`
JPEG base module.

#### constants
##### `JPEG::VERSION`
Version string of this library.

#### module methods
##### `JPEG.read(io)`
Read JPEG file from io and returns `JPEG::Image` object.

`io` must be an `IO` object. It will be binmode'ed.

##### `JPEG.write(img, io, gray = false)`
Write `img` as JPEG file to `io`.

`img` must be a `JPEG::Image` object.
`io` must be an `IO` object. It will be binmode'ed.
`gray` must be a true value or a false value. If `gray` is a true value,
the written JPEG file will be grayscale image.

##### class JPEG::Image
Class for image data.

#### super class
`Object`

#### class methods
##### `JPEG::Image.new`
Create a `JPEG::Image` object.

#### instance methods
##### `JPEG::Image#width`
Returns the width of the image.

##### `JPEG::Image#width=(num)`
Set the width of the image.

`num` must be an `Integer` object. It must be more than 0.

##### `JPEG::Image#height`
Returns the height of the image.

##### `JPEG::Image#width=(num)`
Set the height of the image.

`num` must be an `Integer` object. It must be more than 0.

##### `JPEG::Image#quality`
Returns the quality of the image.

##### `JPEG::Image#quality=(num)`
Set the quality of the image.

`num` must be an `Integer` object. It must be more than 0 and less than or 
equal to 100.

##### `JPEG::Image#raw_data`
Returns the raw RGB data of the image.

The returned value is a `String` object.
If the image is colored, 1 pixel is 3 bytes -- 1st byte means red, 2nd byte
means green, and 3rd byte means blue.
If the image is grayscaled, 1 pixel is 1 byte.

The raw data starts the pixel of left-top corner.
And next 3 bytes (color) or 1 byte (grayscale) is the right pixel of it, and
so on.
After first line ended, the most left pixel of next line starts.

##### `JPEG::Image#raw_data=(str)`
Set the raw RGB data of the image.

`str` must be a `String` object. It must be larger than or equal to the
required size.
You can calculate that the size is width * C * height.
C is 3 if the image is colored, or 1 if the image is grayscaled.

##### `JPEG::Image#gray?`
Returns the image is grayscaled or not.

The returned value is a true value or a false value.

##### `JPEG::Image#bilinear(width, height)`
Creates and returns a new `JPEG::Image` object by converting the size of the
image.
It converts the image by using bilinear operation.

`width` and `height` must be `Integer` objects. They must be more than 0.

##### `JPEG::Image#bicubic(width, height)`
Creates and returns a new `JPEG::Image` object by converting the size of the
image.
It converts the image by using bicubic operation.

`width` and `height` must be `Integer` objects. They must be more than 0.

##### `JPEG::Image#auto_contrast()`
Creates and returns a new `JPEG::Image` object which is adfusted the level of
the image contrast automatically.

##### `JPEG::Image#level(low, high, adjust = false)`
Creates and returns a new `JPEG::Image` object which is cut the level of the
image contrast.

`low` and `high` must be `Integer` objects. They must be more than 0 and less
than or equal to 100. `high` must be more than `low`.
`adjust` must be a true value or a false value. If true, the level of contrast
will be adjusted automatically after cutting.

##### `JPEG::Image#clip(x1 = nil, y1 = nil, x2 = nil, y2 = nil)`
Creates and returns a new `JPEG::Image` object which is clipped from the image.
If there is no argument, returns an image clipped automatically.
Otherwise, requires all 4 arguments and clips (`x1`, `y1`) - (`x2`, `y2`).

`x1`, `y1`, `x2`, and `y2` must be `Integer` objects or nil.

##### `JPEG::Image#grayscale()`
Creates and returns a new `JPEG::Image` object which is grayscaled from the
image.

### class `JPEG::Reader`
Class for reading JPEG file.

#### super class
`Object`

#### class methods
##### `JPEG::Reader.new(io)`
##### `JPEG::Reader.open(io)`
Create and returns a `JPEG::Reader` object.
The object will read a JPEG file from `io`.

`io` must be an `IO` object. It will be binmode'ed.

##### `JPEG::Reader.open(io) {|reader| ... }`
Create a `JPEG::Reader` object and will pass it to the given block.
After executing the block, it returns `nil`.

#### instance methods
##### `JPEG::Reader#close`
Close the object.
You can never use this object to read data.

##### `JPEG::Reader#each {|line| ... }`
##### `JPEG::Reader#each_line {|line| ... }`
##### `JPEG::Reader#read_each_line {|line| ... }`
Reads the JPEG file and passes the raw RGB data of each lines to the block.
The passed `line` will be a `String` object. 
If the image is colored, 1 pixel is 3 bytes -- 1st byte means red, 2nd byte
means green, and 3rd byte means blue.
If the image is grayscaled, 1 pixel is 1 byte.

##### `JPEG::Reader#width`
Returns the width of the image.

##### `JPEG::Reader#height`
Returns the height of the image.

### class `JPEG::Writer`
Class for writing JPEG file.

#### super class
`Object`

#### class methods
##### `JPEG::Writer.new(io, width, height, quality, gray = false)`
##### `JPEG::Writer.open(io, width, height, quality, gray = false)`
Create and returns a `JPEG::Writer` object.
The object will write a JPEG file to `io`.

`io` must be an `IO` object. It will be binmode'ed.
`width` and `hight` must be `Integer` objects. They must be more than 0.
`quality` must be an `Integer` object. It must be more than 0 and less than or 
equal to 100.
`gray` must be a true value or a false value. If `gray` is a true value,
the written JPEG file will be grayscale image.

##### `JPEG::Writer.open(io, width, height, quary, gray = false) {|writer| ... }`
Create a `JPEG::Writer` object and will pass it to the given block.
The object will write a JPEG file to io.

`io` must be an `IO` object. It will be binmode'ed.
`width` and `height` must be `Integer` objects. They must be more than 0.
`quality` must be an `Integer` object. It must be more than 0 and less than or 
equal to 100.
`gray` must be a true value or a false value. If `gray` is a true value,
the written JPEG file will be grayscale image.

After executing the block, it returns `nil`.

#### instance methods
##### `JPEG::Writer#close`
Close the object.
You can never use this object to write data.

##### `JPEG::Writer#write_each_line { ... }`
Write the return value of the block as the raw RGB data of each lines to
the JPEG file.
The return value of the block must be a `String` object. 
If the image is created for grayscale image, 1 pixel is 1 byte.
Otherwise, 1 pixel is 3 bytes -- 1st byte means red, 2nd byte means green,
and 3rd byte means blue.

##### `JPEG::Writer#width`
Returns the width of the image.

##### `JPEG::Writer#height`
Returns the height of the image.

##### `JPEG::Writer#quality`
Returns the quality of the image.

### `JPEG::InternalError`
errors in this library.

#### super class
`StandardError`

### `JPEG::JERR_*`
libjpeg errors.

#### super class
`JPEG::InternalError`

#### constants
##### `Errno`
libjpeg error code.

### `JPEG::UnknownError`
libjpeg unknown error.

#### super class
`JPEG::InternalError`


## LEGAL Issue

Copyright (c) 2007,2011  NAKAMURA Usaku <usa@garbagecollect.jp>
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

(1) Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
(2) Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

### Attention
The binary form of this library may contain the libjpeg's code.
In such case, you might have to consider to the license of libjpeg.
