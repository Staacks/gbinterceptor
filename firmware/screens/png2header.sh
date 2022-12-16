BASE=`echo $1 | cut -f 1 -d '.'`
ffmpeg -y -colorspace bt470bg -i $1 -pix_fmt nv12 $BASE.yuv

xxd -i $BASE.yuv | sed '1 s/unsigned char/unsigned char __in_flash("screens")/' > ${BASE}_yuv.h
rm $BASE.yuv

