# Copyright (c) 2014-2016, The Regents of the University of California.
# Copyright (c) 2016-2017, Nefeli Networks, Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# * Redistributions of source code must retain the above copyright notice, this
# list of conditions and the following disclaimer.
#
# * Redistributions in binary form must reproduce the above copyright notice,
# this list of conditions and the following disclaimer in the documentation
# and/or other materials provided with the distribution.
#
# * Neither the names of the copyright holders nor the names of their
# contributors may be used to endorse or promote products derived from this
# software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

# Copied from https://github.com/benhodgson/protobuf-to-dict (cd98280)
# written by Ben Hodgson, with additional fixes

import sys

from google.protobuf.message import Message
from google.protobuf.descriptor import FieldDescriptor


__all__ = ["protobuf_to_dict", "TYPE_CALLABLE_MAP",
           "dict_to_protobuf", "REVERSE_TYPE_CALLABLE_MAP"]


EXTENSION_CONTAINER = '___X'


TYPE_CALLABLE_MAP = {
    FieldDescriptor.TYPE_DOUBLE: float,
    FieldDescriptor.TYPE_FLOAT: float,
    FieldDescriptor.TYPE_INT32: int,
    FieldDescriptor.TYPE_INT64: int,
    FieldDescriptor.TYPE_UINT32: int,
    FieldDescriptor.TYPE_UINT64: int,
    FieldDescriptor.TYPE_SINT32: int,
    FieldDescriptor.TYPE_SINT64: int,
    FieldDescriptor.TYPE_FIXED32: int,
    FieldDescriptor.TYPE_FIXED64: int,
    FieldDescriptor.TYPE_SFIXED32: int,
    FieldDescriptor.TYPE_SFIXED64: int,
    FieldDescriptor.TYPE_BOOL: bool,
    FieldDescriptor.TYPE_STRING: str,
    FieldDescriptor.TYPE_BYTES: bytes,
    FieldDescriptor.TYPE_ENUM: int,
}


def repeated_map(type_k_callable, type_v_callable):
    return lambda values: {type_k_callable(k): type_v_callable(v)
                           for k, v in values.items()}


def repeated(type_callable):
    return lambda value_list: [type_callable(value) for value in value_list]


def enum_label_name(field, value):
    return field.enum_type.values_by_number[int(value)].name


def protobuf_to_dict(pb, type_callable_map=TYPE_CALLABLE_MAP, use_enum_labels=False):
    result_dict = {}
    extensions = {}
    for field, value in pb.ListFields():
        if field.type == FieldDescriptor.TYPE_MESSAGE and \
                field.message_type.GetOptions().map_entry:
            field_k = field.message_type.fields_by_name['key']
            type_k_callable = _get_field_value_adaptor(pb,
                                                       field_k,
                                                       type_callable_map,
                                                       use_enum_labels)
            field_v = field.message_type.fields_by_name['value']
            type_v_callable = _get_field_value_adaptor(pb,
                                                       field_v,
                                                       type_callable_map,
                                                       use_enum_labels)
            type_callable = repeated_map(type_k_callable, type_v_callable)
        else:
            type_callable = _get_field_value_adaptor(
                pb, field, type_callable_map, use_enum_labels)
            if field.label == FieldDescriptor.LABEL_REPEATED:
                type_callable = repeated(type_callable)

        if field.is_extension:
            extensions[str(field.number)] = type_callable(value)
            continue

        result_dict[field.name] = type_callable(value)

    if extensions:
        result_dict[EXTENSION_CONTAINER] = extensions
    return result_dict


def _get_field_value_adaptor(pb, field, type_callable_map=TYPE_CALLABLE_MAP, use_enum_labels=False):
    if field.type == FieldDescriptor.TYPE_MESSAGE:
        # recursively encode protobuf sub-message
        return lambda pb: protobuf_to_dict(pb,
                                           type_callable_map=type_callable_map,
                                           use_enum_labels=use_enum_labels)

    if use_enum_labels and field.type == FieldDescriptor.TYPE_ENUM:
        return lambda value: enum_label_name(field, value)

    if field.type in type_callable_map:
        return type_callable_map[field.type]

    raise TypeError("Field %s.%s has unrecognised type id %d" % (
        pb.__class__.__name__, field.name, field.type))


REVERSE_TYPE_CALLABLE_MAP = {
    # empty
}


def dict_to_protobuf(pb_klass_or_instance, values, type_callable_map=REVERSE_TYPE_CALLABLE_MAP, strict=True):
    """Populates a protobuf model from a dictionary.

    :param pb_klass_or_instance: a protobuf message class, or an protobuf instance
    :type pb_klass_or_instance: a type or instance of a subclass of google.protobuf.message.Message
    :param dict values: a dictionary of values. Repeated and nested values are
       fully supported.
    :param dict type_callable_map: a mapping of protobuf types to callables for setting
       values on the target instance.
    :param bool strict: complain if keys in the map are not fields on the message.
    """
    if isinstance(pb_klass_or_instance, Message):
        instance = pb_klass_or_instance
    else:
        instance = pb_klass_or_instance()
    return _dict_to_protobuf(instance, values, type_callable_map, strict)


def _get_field_mapping(pb, dict_value, strict):
    field_mapping = []
    for key, value in dict_value.items():
        if key == EXTENSION_CONTAINER:
            continue
        if key not in pb.DESCRIPTOR.fields_by_name:
            if strict:
                raise KeyError("%s does not have a field called %s" %
                               (pb.__class__.__name__, key))
            continue
        field_mapping.append((pb.DESCRIPTOR.fields_by_name[
                             key], value, getattr(pb, key, None)))

    for ext_num, ext_val in dict_value.get(EXTENSION_CONTAINER, {}).items():
        try:
            ext_num = int(ext_num)
        except ValueError:
            raise ValueError("Extension keys must be integers.")
        if ext_num not in pb._extensions_by_number:
            if strict:
                raise KeyError("%s does not have a extension with number %s. "
                               "Perhaps you forgot to import it?" %
                               (pb.__class__.__name__, key))
            continue
        ext_field = pb._extensions_by_number[ext_num]
        pb_val = pb.Extensions[ext_field]
        field_mapping.append((ext_field, ext_val, pb_val))

    return field_mapping


def _dict_to_protobuf(pb, value, type_callable_map, strict):
    fields = _get_field_mapping(pb, value, strict)

    if sys.version_info[0] == 2:
        basestr = basestring
    else:
        basestr = str

    for field, input_value, pb_value in fields:
        if field.label == FieldDescriptor.LABEL_REPEATED:
            if field.message_type and field.message_type.has_options and \
                   field.message_type.GetOptions().map_entry:
                # Special processing for nested dict
                if isinstance(input_value, dict) and all([isinstance(x, dict) for x in input_value.values()]):
                    for k, v in input_value.items():
                        _dict_to_protobuf(
                            pb_value[k], input_value[k], type_callable_map, strict)
                else:
                    pb_value.update(input_value)
                continue
            for item in input_value:
                if field.type == FieldDescriptor.TYPE_MESSAGE:
                    m = pb_value.add()
                    _dict_to_protobuf(m, item, type_callable_map, strict)
                elif field.type == FieldDescriptor.TYPE_ENUM and isinstance(item, basestr):
                    pb_value.append(_string_to_enum(field, item))
                else:
                    pb_value.append(item)
            continue
        if field.type == FieldDescriptor.TYPE_MESSAGE:
            _dict_to_protobuf(pb_value, input_value, type_callable_map, strict)
            continue

        if field.type in type_callable_map:
            input_value = type_callable_map[field.type](input_value)

        if field.is_extension:
            pb.Extensions[field] = input_value
            continue

        if field.type == FieldDescriptor.TYPE_ENUM and isinstance(input_value, basestr):
            input_value = _string_to_enum(field, input_value)

        setattr(pb, field.name, input_value)

    return pb


def _string_to_enum(field, input_value):
    enum_dict = field.enum_type.values_by_name
    try:
        input_value = enum_dict[input_value].number
    except KeyError:
        raise KeyError("`%s` is not a valid value for field `%s`" %
                       (input_value, field.name))
    return input_value
