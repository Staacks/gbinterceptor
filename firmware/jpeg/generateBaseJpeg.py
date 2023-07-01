#This is a simple Python script that creates a JPEG file in the desired format and creates a header file with the data.
#Note that I do not expect any changes here, so this is mostly here for reference and not properly automated.
#If you change anything, you will very likely need to change offsets in the Pico code.

def generateSOI():
    return bytearray([0xff, 0xd8])

def generateAPP0():
    data = bytearray([0xff, 0xe0])
    data.extend([0x00, 0x11])               #length 17
    data.extend(b"JFIF\x00")                #JFIF
    data.extend([0x01, 0x01])               #Version 1.1
    data.extend([0x01])                     #Units DPI
    data.extend([0x00, 0x48, 0x00, 0x48])   #Density 72x72
    data.extend([0x00, 0x00])               #Thumbnail 0x0
    data.extend([0x00])                     #No meaning. We just need to pad the image by one byte to have the data block starting at a multiple of 32.
    return data

def generateDQT():
    data = bytearray([0xff, 0xdb])
    data.extend([0x00, 0x43])               #Length 67
    data.extend([0x00])                     #Destination luminance
    data.extend([0xff]*64)                  #QUantization table entirely filled with 0xff
    return data

def generateSOF():
    data = bytearray([0xff, 0xc0])
    data.extend([0x00, 0x0b])               #Length 11
    data.extend([0x08])                     #Precision
    data.extend([0x04, 0x80])               #height 8*144
    data.extend([0x05, 0x00])               #height 8*160
    data.extend([0x01])                     #Nf
    data.extend([0x01, 0x11, 0x00])         #Luminance channel setup
    return data

def generateDHT_DC():
    data = bytearray([0xff, 0xc4])
    data.extend([0x00, 0x17])               #Length 22
    data.extend([0x00])                     #DC Huffman table, destination 0

    #Huffman table
    #0 -> 0x03
    #10 -> 0x02
    #110 -> 0x01
    #1110 -> 0x00
    data.extend([0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x02, 0x01, 0x00]) 

    return data

def generateDHT_AC():
    data = bytearray([0xff, 0xc4])
    data.extend([0x00, 0x14])               #Length 20
    data.extend([0x10])                     #AC Huffman table, destination 0

    #Huffman table
    data.extend([0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00])

    return data

def generateSOS():
    data = bytearray([0xff, 0xda])
    data.extend([0x00, 0x08])               #Length 8
    data.extend([0x01])                     #Components: 1
    data.extend([0x01, 0x00])               #Component one uses tables 0/0
    data.extend([0x00, 0x3f])               #Spectral select
    data.extend([0x00])                     #seccessive approx
    return data

def encodeVal(v):
    lookup = [0b00000, 0b00010, 0b00100, 0b00110, 0b10000, 0b10010, 0b11000, 0b11100, 0b11010, 0b10100, 0b10110, 0b01000, 0b01010, 0b01100, 0b01110]
    return lookup[v+7]

def generateData():
    last = 0
    pixelData = [0x00]*(160*144*5//8)
    for y in range(144):
        for x in range(160):
            lum = -3 if (x % 8 == 0 or y % 8 == 0) else -3+((x + y) % 8)
            diff = lum - last
            last = lum
            code = encodeVal(diff)
            offset = 5*(160*y+x)
            byteOffset = offset // 8
            bitOffset = offset % 8
            if bitOffset <= 3:
                pixelData[byteOffset] |= (code << (3-bitOffset))
            else:
                pixelData[byteOffset] |= (code >> (bitOffset-3))
                pixelData[byteOffset+1] |= ((code << (11-bitOffset)) & 0xff)
    return pixelData

def generateEOI():
    return bytearray([0xff, 0xd9])

data = bytearray()
data.extend(generateSOI())
data.extend(generateAPP0())
data.extend(generateDQT())
data.extend(generateSOF())
data.extend(generateDHT_DC())
data.extend(generateDHT_AC())
data.extend(generateSOS())
data.extend(generateData())
data.extend(generateEOI())



with open("base.jpg", "wb") as f:
    f.write(data)

with open("base_jpeg.h", "w") as f:
    f.write("unsigned char __in_flash(\"jpeg\") base_jpeg[] = {")
    for i in range(len(data)):
        if i % 16 == 0:
            f.write("\n")
        f.write(' 0x{:02x},'.format(data[i]))
    f.write("\n};\n")

