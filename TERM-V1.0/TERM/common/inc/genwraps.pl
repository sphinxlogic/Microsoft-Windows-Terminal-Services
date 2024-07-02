# genwraps.pl
# helps generate wrappers for a module's imports
#input an imports file
#generated by 'link /dump /imports MODULE'
#outputs the replacer macros that should go in uwrap.h
# nadima
while(<>)
{
   #look for a single line containing a dll
   if(m/\W*(\w*\.dll)$/)
   {
      print "// ". $1 ."\n";
   }
   #look for a 'W' function
   if(m/.* (\w*)W$/)
   {
      print "#ifdef  " . $1 . "\n";
      print "#undef  " . $1 . "\n";
      print "#endif\n";
      print "#define " . $1 . " " . $1 . "XWrap\n\n" ;
   }

}