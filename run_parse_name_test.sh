#!/bin/bash
cd crypto/external/bsd/heimdal/dist/lib/krb5
# Wait, this is part of the heimdal kerberos distribution. We can't easily compile it standalone because it depends on internal headers like `krb5_locl.h`.
