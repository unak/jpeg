require "jpeg"
dir = File.dirname(__FILE__)

puts "jpeg.so version = #{JPEG::VERSION}"

src = nil
File.open(File.join(dir, "test.jpg"), "rb") do |f|
  src = JPEG.read(f)
end
puts "source   : %d x %d, %d bytes (%sgray)" % [src.width, src.height, src.raw_data.size, src.gray?? "" : "not "]

dest = src.bilinear(src.width / 3, src.height / 3)
puts "bilinear : %d x %d, %d bytes (test2.jpg)" % [dest.width, dest.height, dest.raw_data.size]
dest.quality = 100
File.open("test2.jpg", "wb") do |f|
  JPEG.write(dest, f)
end

File.open("test2.jpg", "rb") do |f|
  begin
    JPEG::Reader.open(f) do |reader|
      puts "test2.jpg: %d x %d" % [reader.width, reader.height]
      reader.each do |line|
        puts "  line size = #{line.size} (total = #{line.size * reader.height})"
        break
      end
    end
  rescue JPEG::JERR_TOO_LITTLE_DATA
    # break in each makes this exception
  end
end

dest = src.bicubic(src.width / 3, src.height / 3)
puts "bicubic  : %d x %d, %d bytes (test3.jpg)" % [dest.width, dest.height, dest.raw_data.size]
dest.quality = 100
File.open("test3.jpg", "wb") do |f|
  JPEG.write(dest, f)
end

dest = src.auto_contrast.bicubic(src.width / 3, src.height / 3)
dest.quality = 100
File.open("test4.jpg", "wb") do |f|
  JPEG.write(dest, f)
  puts "test4.jpg: auto contrasted image"
end

gray = src.grayscale.bilinear(src.width / 3, src.height / 3)
gray.quality = 100
File.open("test5.jpg", "wb") do |f|
  JPEG.write(gray, f)
  puts "test5.jpg: grayscaled image"
end
File.open("test5.jpg", "rb") do |f|
  src2 = JPEG.read(f)
  puts "         : %d x %d, %d bytes (%sgray)" % [src2.width, src2.height, src2.raw_data.size, src2.gray?? "" : "not "]
end

dest = src.grayscale.auto_contrast.bilinear(src.width / 3, src.height / 3)
dest.quality = 100
File.open("test6.jpg", "wb") do |f|
  JPEG.write(dest, f)
  puts "test6.jpg: grayed and auto contrasted image"
end

File.open("test7.jpg", "wb") do |f|
  JPEG::Writer.open(f, 320, 240, 50) do |writer|
    puts "test7.jpg: %d x %d (%d)" % [writer.width, writer.height, writer.quality]
    r = 0
    g = 0
    b = 0
    writer.write_each_line do
      line = ""
      320.times do
        line << r.chr << g.chr << b.chr
        r += 8
        if r > 255
          r = 0
          g += 8
          if g > 255
            g = 0
            b += 8
            if b > 255
              b = 0
            end
          end
        end
      end
      line
    end
  end
end

File.open("test8.jpg", "wb") do |f|
  JPEG::Writer.open(f, 320, 240, 50, true) do |writer|
    puts "test8.jpg: %d x %d (%d)" % [writer.width, writer.height, writer.quality]
    scale = 0
    writer.write_each_line do
      line = ""
      320.times do
        line << scale.chr
        scale += 1;
        scale = 0 if scale > 255
      end
      line
    end
  end
end


puts "resize benchmark"
require "benchmark"

src = nil
File.open(File.join(dir, "test.jpg"), "rb") do |f|
  src = JPEG.read(f)
end
width = src.width / 3
height = src.height / 3
gray = src.grayscale

TRY = 20
Benchmark.bm do |bm|
  bm.report("bilinear(color):") do
    TRY.times do
      src.bilinear(width, height)
    end
  end

  bm.report("bicubic (color):") do
    TRY.times do
      src.bicubic(width, height)
    end
  end

  bm.report("bilinear(gray) :") do
    TRY.times do
      gray.bilinear(width, height)
    end
  end

  bm.report("bicubic (gray) :") do
    TRY.times do
      gray.bicubic(width, height)
    end
  end

  bm.report("gray special   :") do
    TRY.times do
      gray.auto_contrast.bilinear(width, height)
    end
  end
end
