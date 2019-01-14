#
# Copyright(c) 2019 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

from ctypes import *

from ..ocf import OcfLib
from .shared import Uuid
from .data_object import DataObject
from .data import Data
from .io import Io, IoDir


class UserMetadata(Structure):
    _fields_ = [("data", c_void_p), ("size", c_size_t)]


class CoreConfig(Structure):
    _fields_ = [
        ("_uuid", Uuid),
        ("_data_obj_type", c_uint8),
        ("_core_id", c_uint16),
        ("_name", c_char_p),
        ("_cache_id", c_uint16),
        ("_try_add", c_bool),
        ("_seq_cutoff_threshold", c_uint32),
        ("_user_metadata", UserMetadata),
    ]


class ExportedObject:
    pass


class Core:
    DEFAULT_ID = 4096
    DEFAULT_SEQ_CUTOFF_THRESHOLD = 1024 * 1024

    def __init__(
        self,
        *,
        device: DataObject,
        core_id: int,
        name: str,
        try_add: bool,
        seq_cutoff_threshold: int
    ):

        self.device = device
        self.device_name = device.uuid
        self.name = name
        self.core_id = core_id
        self.handle = c_void_p()
        self.cfg = CoreConfig(
            _uuid=Uuid(
                _data=cast(
                    create_string_buffer(self.device_name.encode("ascii")), c_char_p
                ),
                _size=len(self.device_name) + 1,
            ),
            _core_id=self.core_id,
            _data_obj_type=self.device.type_id,
            _try_add=try_add,
            _seq_cutoff_threshold=seq_cutoff_threshold,
            _user_metadata=UserMetadata(_data=None, _size=0),
        )

    @classmethod
    def using_device(
        cls,
        device,
        name: str = "",
        core_id: int = DEFAULT_ID,
        seq_cutoff_threshold: int = DEFAULT_SEQ_CUTOFF_THRESHOLD,
    ):
        c = cls(
            device=device,
            core_id=core_id,
            name=name,
            try_add=False,
            seq_cutoff_threshold=seq_cutoff_threshold,
        )

        return c

    def get_cfg(self):
        return self.cfg

    def get_handle(self):
        return self.handle

    def new_io(self):
        io = OcfLib.getInstance().ocf_core_new_io_wrapper(self.handle)
        return Io.from_pointer(io)