vim9script

def Test(findstart: number, base: string): any
  if findstart == 1
    const b = substitute(getline('.')[: col(".")], "'", "\\'", "g")
    if b !~ '^# import '
      return -2
    endif

    return len("# import")
  endif

  var list = systemlist('out/arm64-apple-macosx15/classdb.exe search "' .. base .. '"')
  map(list, (k, v) => substitute(v, "[/$]", ".", "g"))
  return list
enddef

set omnifunc=Test

# just try omni here
#
# import test
