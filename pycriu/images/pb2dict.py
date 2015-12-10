from google.protobuf.descriptor import FieldDescriptor as FD
import opts_pb2
import ipaddr
import socket
import collections

# pb2dict and dict2pb are methods to convert pb to/from dict.
# Inspired by:
#   protobuf-to-dict - https://github.com/benhodgson/protobuf-to-dict
#   protobuf-json    - https://code.google.com/p/protobuf-json/
#   protobuf source  - https://code.google.com/p/protobuf/
# Both protobuf-to-dict/json do not fit here because of several reasons,
# here are some of them:
#   - both have a common bug in treating optional field with empty
#     repeated inside.
#   - protobuf-to-json is not avalible in pip or in any other python
#     repo, so it is hard to distribute and we can't rely on it.
#   - both do not treat enums in a way we would like to. They convert
#     protobuf enum to int, but we need a string here, because it is
#     much more informative. BTW, protobuf text_format converts pb
#     enums to string value too. (i.e. "march : x86_64" is better then
#     "march : 1").


_basic_cast = {
	FD.TYPE_DOUBLE		: float,
	FD.TYPE_FLOAT		: float,
	FD.TYPE_FIXED64		: float,
	FD.TYPE_FIXED32		: float,
	FD.TYPE_SFIXED64	: float,
	FD.TYPE_SFIXED32	: float,

	FD.TYPE_INT64		: long,
	FD.TYPE_UINT64		: long,
	FD.TYPE_SINT64		: long,

	FD.TYPE_INT32		: int,
	FD.TYPE_UINT32		: int,
	FD.TYPE_SINT32		: int,

	FD.TYPE_BOOL		: bool,

	FD.TYPE_STRING		: unicode
}

def _marked_as_hex(field):
	return field.GetOptions().Extensions[opts_pb2.criu].hex

def _marked_as_ip(field):
	return field.GetOptions().Extensions[opts_pb2.criu].ipadd

def _marked_as_flags(field):
	return field.GetOptions().Extensions[opts_pb2.criu].flags

mmap_prot_map = [
	('PROT_READ',	0x1),
	('PROT_WRITE',	0x2),
	('PROT_EXEC',	0x4),
];

mmap_flags_map = [
	('MAP_SHARED',	0x1),
	('MAP_PRIVATE',	0x2),
	('MAP_ANON',	0x20),
];

rfile_flags_map = [
	('O_WRONLY',	01),
	('O_RDWR',	02),
	('O_APPEND',	02000),
	('O_DIRECT',	040000),
	('O_LARGEFILE',	0100000),
];

flags_maps = {
	'mmap.prot' : mmap_prot_map,
	'mmap.flags' : mmap_flags_map,
	'rfile.flags' : rfile_flags_map,
}

def map_flags(value, flags_map):
	bs = map(lambda x: x[0], filter(lambda x: value & x[1], flags_map))
	value &= ~sum(map(lambda x: x[1], flags_map))
	if value:
		bs.append("0x%x" % value)
	return " | ".join(bs)

def unmap_flags(value, flags_map):
	if value == '':
		return 0

	bd = dict(flags_map)
	return sum(map(lambda x: int(str(bd.get(x, x)), 0), map(lambda x: x.strip(), value.split('|'))))

def _pb2dict_cast(field, value, pretty = False, is_hex = False):
	if not is_hex:
		is_hex = _marked_as_hex(field)

	if field.type == FD.TYPE_MESSAGE:
		return pb2dict(value, pretty, is_hex)
	elif field.type == FD.TYPE_BYTES:
		return value.encode('base64')
	elif field.type == FD.TYPE_ENUM:
		return field.enum_type.values_by_number.get(value, None).name
	elif field.type in _basic_cast:
		cast = _basic_cast[field.type]
		if pretty and (cast == int or cast == long):
			if is_hex:
				# Fields that have (criu).hex = true option set
				# should be stored in hex string format.
				return "0x%x" % value

			flags = _marked_as_flags(field)
			if flags:
				try:
					flags_map = flags_maps[flags]
				except:
					return "0x%x" % value # flags are better seen as hex anyway
				else:
					return map_flags(value, flags_map)

		return cast(value)
	else:
		raise Exception("Field(%s) has unsupported type %d" % (field.name, field.type))

def pb2dict(pb, pretty = False, is_hex = False):
	"""
	Convert protobuf msg to dictionary.
	Takes a protobuf message and returns a dict.
	"""
	d = collections.OrderedDict() if pretty else {}
	for field, value in pb.ListFields():
		if field.label == FD.LABEL_REPEATED:
			d_val = []
			if pretty and _marked_as_ip(field):
				if len(value) == 1:
					v = socket.ntohl(value[0])
					addr = ipaddr.IPv4Address(v)
				else:
					v = 0 +	(socket.ntohl(value[0]) << (32 * 3)) + \
						(socket.ntohl(value[1]) << (32 * 2)) + \
						(socket.ntohl(value[2]) << (32 * 1)) + \
						(socket.ntohl(value[3]))
					addr = ipaddr.IPv6Address(v)

				d_val.append(addr.compressed)
			else:
				for v in value:
					d_val.append(_pb2dict_cast(field, v, pretty, is_hex))
		else:
			d_val = _pb2dict_cast(field, value, pretty, is_hex)

		d[field.name] = d_val
	return d

def _dict2pb_cast(field, value):
	# Not considering TYPE_MESSAGE here, as repeated
	# and non-repeated messages need special treatment
	# in this case, and are hadled separately.
	if field.type == FD.TYPE_BYTES:
		return value.decode('base64')
	elif field.type == FD.TYPE_ENUM:
		return field.enum_type.values_by_name.get(value, None).number
	elif field.type in _basic_cast:
		cast = _basic_cast[field.type]
		if (cast == int or cast == long) and isinstance(value, unicode):
			flags = _marked_as_flags(field)
			if flags:
				try:
					flags_map = flags_maps[flags]
				except:
					pass # Try to use plain string cast
				else:
					return unmap_flags(value, flags_map)

			# Some int or long fields might be stored as hex
			# strings. See _pb2dict_cast.
			return cast(value, 0)
		else:
			return cast(value)
	else:
		raise Exception("Field(%s) has unsupported type %d" % (field.name, field.type))

def dict2pb(d, pb):
	"""
	Convert dictionary to protobuf msg.
	Takes dict and protobuf message to be merged into.
	"""
	for field in pb.DESCRIPTOR.fields:
		if field.name not in d:
			continue
		value = d[field.name]
		if field.label == FD.LABEL_REPEATED:
			pb_val = getattr(pb, field.name, None)
			for v in value:
				if field.type == FD.TYPE_MESSAGE:
					dict2pb(v, pb_val.add())
				else:
					pb_val.append(_dict2pb_cast(field, v))
		else:
			if field.type == FD.TYPE_MESSAGE:
				# SetInParent method acts just like has_* = true in C,
				# and helps to properly treat cases when we have optional
				# field with empty repeated inside.
				getattr(pb, field.name).SetInParent()

				dict2pb(value, getattr(pb, field.name, None))
			else:
				setattr(pb, field.name, _dict2pb_cast(field, value))
	return pb
