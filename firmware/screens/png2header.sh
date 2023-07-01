BASE=`echo $1 | cut -f 1 -d '.'`
convert $1 -brightness-contrast -98x-98 gray:$BASE.raw

xxd -i $BASE.raw | sed '1 s/unsigned char/unsigned char __in_flash("screens")/' > ${BASE}.h
rm $BASE.raw

