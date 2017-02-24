from google.protobuf.message import Message
from google.protobuf.descriptor import FieldDescriptor

__all__ = ["protobuf_to_dict", "dict_to_protobuf"]

EXTENSION_CONTAINER = '___X'

TYPE_CALLABLE_MAP = {
    FieldDescriptor.TYPE_DOUBLE: float,
    FieldDescriptor.TYPE_FLOAT: float,
    FieldDescriptor.TYPE_INT32: int,
    FieldDescriptor.TYPE_INT64: long,
    FieldDescriptor.TYPE_UINT32: int,
    FieldDescriptor.TYPE_UINT64: long,
    FieldDescriptor.TYPE_SINT32: int,
    FieldDescriptor.TYPE_SINT64: long,
    FieldDescriptor.TYPE_FIXED32: int,
    FieldDescriptor.TYPE_FIXED64: long,
    FieldDescriptor.TYPE_SFIXED32: int,
    FieldDescriptor.TYPE_SFIXED64: long,
    FieldDescriptor.TYPE_BOOL: bool,
    FieldDescriptor.TYPE_STRING: str,
    FieldDescriptor.TYPE_BYTES: str,
    FieldDescriptor.TYPE_ENUM: int,
}


def repeated(type_callable):
    return lambda value_list: [type_callable(value) for value in value_list]


def field_value_adaptor(field):
    if field.type == FieldDescriptor.TYPE_MESSAGE:
        # recursively encode protobuf sub-message
        return lambda pb: protobuf_to_dict(pb)

    if field.type in TYPE_CALLABLE_MAP:
        return TYPE_CALLABLE_MAP[field.type]

    raise TypeError("Field %s has unrecognised type id %d" %
                    (field.name, field.type))


def protobuf_to_dict(pb):
    result_dict = {}
    # Since pb.ListFields() does not return fields with default value,
    # use pb.DESCRIPTOR.fields instead.
    for field in pb.DESCRIPTOR.fields:
        value = getattr(pb, field.name)
        type_callable = field_value_adaptor(field)
        if field.label == FieldDescriptor.LABEL_REPEATED:
            type_callable = repeated(type_callable)
        result_dict[field.name] = type_callable(value)
    return result_dict


def parse_list(values, message):
    if values is None:
        return

    if isinstance(values[0], dict):
        for v in values:
            cmd = message.add()
            parse_dict(v, cmd)
    else:
        message.extend(values)


def parse_dict(values, message):
    if values is None:
        return

    for k, v in values.iteritems():
        if isinstance(v, dict):
            parse_dict(v, getattr(message, k))
        elif isinstance(v, list):
            parse_list(v, getattr(message, k))
        elif v is not None:
            if hasattr(message, k):
                setattr(message, k, v)
            else:
                raise KeyError("%s does not have a field called %s" % (message,
                                                                       k))
        else:
            pass


def dict_to_protobuf(values, msg_type):
    message = msg_type()
    parse_dict(values, message)
    return message
