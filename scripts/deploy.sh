#!/bin/bash

#set -euxo

ssh-keyscan etf2.org
ssh etf@etf2.org mkdir /tmp/etfdeploy
scp $TRAVIS_BUILD_DIR/*.7z etf@etf2.org:/tmp/etfdeploy
ssh etf@etf2.org 7z x /tmp/etfdeploy/*.7z
ssh etf@etf2.org cp /tmp/etfdeploy/ete-ded.x86 ~/wolfet
ssh etf@etf2.org rm -rf /tmp/etfdeploy
ssh etf@etf2.org sudo systemctl restart etf.service

echo "Deployment successful"
