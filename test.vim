vim9script

def Test(findstart: number, base: string): any
  if findstart == 1
    const b = substitute(getline('.')[: col(".")], "'", "\\'", "g")
    echom b
    return system('out/arm64-apple-macosx15/omni.exe 1 "' .. b .. '"')
  endif

  return systemlist('out/arm64-apple-macosx15/omni.exe 0 "' .. base .. '"')
enddef

set omnifunc=Test

# just try omni here
