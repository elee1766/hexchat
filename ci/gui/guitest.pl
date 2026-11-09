Xchat::register("guitest-pl", "1", "GUI test helper");
open(my $f, ">>", Xchat::get_info("configdir") . "/guitest.out") or die;
print $f "perl-loaded\n";
close($f);
