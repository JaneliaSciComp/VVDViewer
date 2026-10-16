#!/bin/sh
# VVDViewer launcher (Linux binary package).
#
# The application and the libraries it bundles live in data/ next to this
# script. GTK and GLib look for their data files (GSettings schemas, icon
# themes) in the XDG data directories, so prepend the bundled data/share and
# keep the system directories for icon themes and fonts.
HERE="$(dirname "$(readlink -f "$0")")"
export XDG_DATA_DIRS="$HERE/data/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
export GSETTINGS_SCHEMA_DIR="$HERE/data/share/glib-2.0/schemas"
exec "$HERE/data/VVDViewer" "$@"
