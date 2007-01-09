require "jpeg"

dir = File.dirname(__FILE__)

unless String.new.respond_to?(:ord)
  class Integer
    def ord
      self
    end
  end
end

src = nil
File.open(File.join(dir, "test.jpg"), "rb") do |f|
  src = JPEG.read(f)
end
p [src.width, src.height, src.raw_data.size]

dest = src.bilinear(src.width / 3, src.height / 3)
p [dest.width, dest.height, dest.raw_data.size]
dest.quality = 50
File.open(File.join(dir, "test2.jpg"), "wb") do |f|
  JPEG.write(dest, f)
end

File.open(File.join(dir, "test2.jpg"), "rb") do |f|
  JPEG::Reader.open(f) do |reader|
    p [reader.width, reader.height]
    reader.each do |line|
      p line.size
      break
    end
  end
end

File.open(File.join(dir, "test3.jpg"), "wb") do |f|
  JPEG::Writer.open(f, 320, 240, 50) do |writer|
    p [writer.width, writer.height, writer.quality]
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
