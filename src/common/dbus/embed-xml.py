import json, pathlib, sys
pathlib.Path(sys.argv[2]).write_text("static const char remote_object_xml[] = " + json.dumps(pathlib.Path(sys.argv[1]).read_text()) + ";\n")
