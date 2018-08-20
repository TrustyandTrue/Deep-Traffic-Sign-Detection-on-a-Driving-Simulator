#!/bin/bash
# Note:
# The usage of this script is only experimental. In general you want to generate .prototxt files automatically.
# Usage:
# ./experiments/scripts/adapt_network_model.sh DIR FROM TO
#
# Example:
# ./experiments/scripts/adapt_network_model.sh ResNet-50/rfcn_end2end/ 21 14
#

set -x
set -e

DIR=$1
FROM=$2
TO=$3

for i in $(grep -rl ${FROM} ${DIR}); do sed -i.bak "s/${FROM}/${TO}/g" $i; rm $i.bak; done
