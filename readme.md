a custom smartwatch operating system designed to integrate with a variety of external hardware in the tasks:
-normal smartwatch use
-flipper zero like utilities (no skiddie bullshit)
-remote controll and wide communication (lora/bt/wifi/infared/what you connect)
-soon: external hardware support for random stuff+osciliscope+electronic toolkit
-soon: shell cmds/elif/etc
-partially working: microsd storage; will add unix-like cmd's soon?

the general goal is to have a mini cyberdeck thingy/non shitty smartwatch.
far too many smartwatches focus on being a second, shittier phone strapped to your wrist.
 why not do something different


currently working:
lcd and window enviroment: benchmarked at 100fps on 240*280; limited to 45 fps via configuration....
....might need a minor revision (it is double buffered, which eats a whole megabyte of the 16mb psram) 
i've done a lot of hacks to keep the ram free, but when i add the document viewwer app i may try doing things like having a pointer to the document, and only loading what you see and then a little more, like swap space. or else this would be of very poor quality and lag. 

applications: sorta works, it's a new system i'm still getting used to even though i just made it, huh
input handler: only buttons and knobs so far. keyboard/mouse/touchscreen/using your phone soon-ish
