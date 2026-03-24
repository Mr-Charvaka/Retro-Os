for f in src/libc/posix/*.c; do
  fname=$(basename "$f" .c)
  # Ensure we don't undef things that aren't functions
  if [ "$fname" != "nano-mallocr" ] && [ "$fname" != "reent" ]; then
    sed -i "1i#undef $fname" "$f"
  fi
  sed -i 's/#error.*//' "$f"
done
