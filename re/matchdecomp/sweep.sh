# sweep.sh "<flags>" -- runs every case in cases.txt (file func va)
while read -r f fn va; do
  [ -z "$f" ] && continue
  uv run --no-project -q --with capstone --with pefile python match.py "${DREAMS_OUT:-../../out}/recomp/matchdecomp/src/$f.c" $fn $va --flags "$1" --quiet
done < cases.txt
