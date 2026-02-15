unzip bazel-bin/unix/mozc.zip -d /tmp/mozc-install/
sudo install -m 755 /tmp/mozc-install/usr/lib/mozc/mozc_server /usr/lib/mozc/mozc_server
sudo install -m 755 /tmp/mozc-install/usr/lib/mozc/mozc_tool /usr/lib/mozc/mozc_tool
sudo install -m 755 /tmp/mozc-install/usr/lib/mozc/mozc_renderer /usr/lib/mozc/mozc_renderer
sudo install -m 755 /tmp/mozc-install/usr/lib/ibus-mozc/ibus-engine-mozc /usr/lib/ibus-mozc/ibus-engine-mozc
sudo install -m 644 /tmp/mozc-install/usr/share/ibus/component/mozc.xml /usr/share/ibus/component/mozc.xml
sudo cp -r /tmp/mozc-install/usr/share/ibus-mozc/* /usr/share/ibus-mozc/
sudo cp -r /tmp/mozc-install/usr/share/icons/mozc/* /usr/share/icons/mozc/
ibus write-cache
ibus restart
ibus-setup
