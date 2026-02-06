# SPDX-License-Identifier: GPL-2.0+
# Copyright (c) 2026 Texas Instruments Incorporated - https://www.ti.com/
# Written by T Pratham <t-pratham@ti.com>
#
# Entry-type module for private key file for signing images
#

from binman.etype.blob_named_by_arg import Entry_blob_named_by_arg

class Entry_signing_key_path(Entry_blob_named_by_arg):
	"""Private key file path for signing images

	Properties / Entry arguments:
		- signing-key-path: Path to the private key file

	This entry holds the private key file used for signing images.

	Typical usage of this is to provide the full path to the key file in
	SIGNING_KEY make argument during build. If this is not provided, the entry will
	fallback to using the key file specified in the binman node filename property::

		binman {
			signing-key-path {
					filename = "default_key.pem";
			};
		};
	"""
	def __init__(self, section, etype, node):
		super().__init__(section, etype, node, 'signing-key')
		self.external = True
