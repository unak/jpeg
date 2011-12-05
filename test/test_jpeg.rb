require "jpeg"
dir = File.dirname(__FILE__)

puts "jpeg.so version = #{JPEG::VERSION}"

src = nil
File.open(File.join(dir, "test.jpg"), "rb") do |f|
  src = JPEG.read(f)
end
puts "source   : %d x %d, %d bytes" % [src.width, src.height, src.raw_data.size]

dest = src.bilinear(src.width / 3, src.height / 3)
puts "bilinear : %d x %d, %d bytes (test2.jpg)" % [dest.width, dest.height, dest.raw_data.size]
dest.quality = 100
File.open("test2.jpg", "wb") do |f|
  JPEG.write(dest, f)
end

dest = src.bicubic(src.width / 3, src.height / 3)
puts "bicubic  : %d x %d, %d bytes (test3.jpg)" % [dest.width, dest.height, dest.raw_data.size]
dest.quality = 100
File.open("test3.jpg", "wb") do |f|
  JPEG.write(dest, f)
end
File.open("test4.jpg", "wb") do |f|
  JPEG.write(dest, f, true)
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

File.open("test5.jpg", "wb") do |f|
  JPEG::Writer.open(f, 320, 240, 50) do |writer|
    puts "test5.jpg: %d x %d (%d)" % [writer.width, writer.height, writer.quality]
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

File.open("test6.jpg", "wb") do |f|
  JPEG::Writer.open(f, 320, 240, 50, true) do |writer|
    puts "test6.jpg: %d x %d (%d)" % [writer.width, writer.height, writer.quality]
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
