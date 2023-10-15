# Set host platform
HOST_PLATFORM="linux"
if [ "$(uname)" == "Darwin" ]; then
    HOST_PLATFORM="macos"
fi

# Ensure tools are set
if [ -z ${ANDROID_NDK_ROOT+x} ]; then echo "ANDROID_NDK_ROOT is unset"; exit 1; fi
if [ -z ${ANDROID_SDK_ROOT+x} ]; then echo "ANDROID_SDK_ROOT is unset"; exit 1; fi
if [ -z ${JAVA_HOME+x} ]; then echo "JAVA_HOME is unset"; exit 1; fi

OLD_PATH="$(pwd)"
OWN_PATH="$( cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
EXPORT_TEMPLATE_PATH="$HOME/.local/share/godot/export_templates/"
if [ "$(uname)" == "Darwin" ]; then
    EXPORT_TEMPLATE_PATH="$HOME/Library/Application Support/Godot/export_templates/"
fi

# Determine version
cd $OWN_PATH
MAJOR=$(cat version.py | grep major | awk '{print $NF}')
MINOR=$(cat version.py | grep minor | awk '{print $NF}')
STATUS=$(cat version.py | grep status | awk '{print $NF}' | tr -d '"')

GODOT_VERSION="$MAJOR.$MINOR.$STATUS"
echo "Building godot editor $GODOT_VERSION"

# Clear and build the editor
killall godot
rm -rf $OWN_PATH/bin/*
scons

echo "Building web libraries"

source "$HOME/git/emsdk/emsdk_env.sh"
emsdk activate
scons platform=web target=template_debug
scons platform=web target=template_release
mv $OWN_PATH/bin/godot.web.template_release.wasm32.zip $OWN_PATH/bin/web_release.zip
mv $OWN_PATH/bin/godot.web.template_debug.wasm32.zip $OWN_PATH/bin/web_debug.zip

echo "Building android libraries"

# Prepare the Android libraries
scons platform=android target=template_release arch=arm64v8
scons platform=android target=template_release arch=arm64v8 generate_apk=yes
scons platform=android target=template_debug arch=arm64v8
scons platform=android target=template_debug arch=arm64v8 generate_apk=yes
# scons platform=android target=template_release arch=x86_64
# scons platform=android target=template_release arch=x86_64 generate_apk=yes
# scons platform=android target=template_debug arch=x86_64
# scons platform=android target=template_debug arch=x86_64 generate_apk=yes

echo "Installing outputs"

rm -rf "$EXPORT_TEMPLATE_PATH/$GODOT_VERSION"
mkdir -p "$EXPORT_TEMPLATE_PATH/$GODOT_VERSION"
cp $OWN_PATH/bin/android_* "$EXPORT_TEMPLATE_PATH/$GODOT_VERSION/"
cp $OWN_PATH/bin/godot-lib* "$EXPORT_TEMPLATE_PATH/$GODOT_VERSION/"
cp $OWN_PATH/bin/web_*.zip "$EXPORT_TEMPLATE_PATH/$GODOT_VERSION/"
if [ "$(uname)" == "Darwin" ]; then
    cp $OWN_PATH/bin/godot.macos.editor.arm64 ~/bin/godot
else
    cp $OWN_PATH/bin/godot.linuxbsd.editor.x86_64 ~/bin/godot
fi

cd $OLD_PATH
