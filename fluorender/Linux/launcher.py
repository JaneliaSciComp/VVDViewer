#!/usr/bin/python3
import sys, os
if getattr(sys, 'frozen', False):
    bundle_dir = sys._MEIPASS
else:
    bundle_dir = os.path.dirname(os.path.abspath(__file__))

print( 'bundle dir is', bundle_dir )
print( 'sys.executable is', os.path.dirname(sys.executable) )

print(f"Arguments count: {len(sys.argv)}")
for i, arg in enumerate(sys.argv):
    print(f"Argument {i:>6}: {arg}")

dir = os.path.dirname(sys.executable)

command = "export GDK_PIXBUF_MODULE_FILE=" + dir + "/data/loaders.cache" + " && "
command += "export XDG_DATA_DIRS=\"" + dir + "/data:" + dir + "/data/schemas:" + "/usr/shre/gnome:/usr/local/share:/usr/share\"" + " && "
command += dir + "/data/VVDViewer "
for i, arg in enumerate(sys.argv):
    if i > 0:
        command += arg + " "

os.system(command)
