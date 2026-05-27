#!/bin/bash
xgettext --keyword=_ --language=C --from-code=UTF-8 --add-comments -o karton-polkit.pot ../src/*.c
LOCALES="en pl de fr es pt zh_CN ru uk it ja ko ar hi"
echo "$LOCALES" | tr ' ' '\n' > LINGUAS
for lang in $LOCALES; do
    if [ ! -f $lang.po ]; then
        msginit --no-translator -l $lang.UTF-8 -o $lang.po -i karton-polkit.pot
    else
        msgmerge -U $lang.po karton-polkit.pot
    fi
done
