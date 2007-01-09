require "mkmf"

dir_config("jpeg")
if have_header("jpeglib.h") && have_header("jerror.h") &&
   (have_library("jpeg", "jpeg_set_defaults") ||
    have_library("libjpeg", "jpeg_set_defaults"))
  create_makefile("jpeg")
end
