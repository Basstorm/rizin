// SPDX-FileCopyrightText: 2021 deroad <wargio@libero.it>
// SPDX-License-Identifier: LGPL-3.0-only

#include "class_bin.h"
#include "class_private.h"

#define java_class_is_older_format(bin) \
	((bin)->major_version < (45) || \
		((bin)->major_version == (45) && (bin)->minor_version < (3)))

#define CLASS_ACCESS_FLAGS_SIZE 16
static const AccessFlagsReadable access_flags_list[CLASS_ACCESS_FLAGS_SIZE] = {
	{ ACCESS_FLAG_PUBLIC, /*    */ "public" },
	{ ACCESS_FLAG_PRIVATE, /*   */ "private" },
	{ ACCESS_FLAG_PROTECTED, /* */ "protected" },
	{ ACCESS_FLAG_STATIC, /*    */ "static" },
	{ ACCESS_FLAG_FINAL, /*     */ "final" },
	{ ACCESS_FLAG_SUPER, /*     */ "super" },
	{ ACCESS_FLAG_BRIDGE, /*    */ "bridge" },
	{ ACCESS_FLAG_VARARGS, /*   */ "varargs" },
	{ ACCESS_FLAG_NATIVE, /*    */ "native" },
	{ ACCESS_FLAG_INTERFACE, /* */ "interface" },
	{ ACCESS_FLAG_ABSTRACT, /*  */ "abstract" },
	{ ACCESS_FLAG_STRICT, /*    */ "strict" },
	{ ACCESS_FLAG_SYNTHETIC, /* */ "synthetic" },
	{ ACCESS_FLAG_ANNOTATION, /**/ "annotation" },
	{ ACCESS_FLAG_ENUM, /*      */ "enum" },
	{ ACCESS_FLAG_MODULE, /*    */ "module" },
};

static const ConstPool *java_class_constant_pool_at(RzBinJavaClass *bin, ut32 index) {
	if (bin->constant_pool && index < bin->constant_pool_count) {
		return bin->constant_pool[index];
	}
	return NULL;
}

static char *java_class_constant_pool_stringify_at(RzBinJavaClass *bin, ut32 index) {
	const ConstPool *cpool = java_class_constant_pool_at(bin, index);
	if (!cpool) {
		return NULL;
	}
	return java_constant_pool_stringify(cpool);
}

static ut32 sanitize_size(st64 buffer_size, ut32 count, ut32 min_struct_size) {
	ut64 memory_size = count * min_struct_size;
	return memory_size <= buffer_size ? count : 0;
}

static bool java_class_parse(RzBinJavaClass *bin, ut64 base, Sdb *kv, RzBuffer *buf, ut64 *size) {
	// https://docs.oracle.com/javase/specs/jvms/se15/html/jvms-4.html#jvms-4.1
	ut64 offset = 0;
	st64 buffer_size = rz_buf_size(buf);
	if (buffer_size < 1) {
		rz_warn_if_reached();
		goto java_class_parse_bad;
	}

	bin->magic = rz_buf_read_be32(buf);
	bin->minor_version = rz_buf_read_be16(buf);
	bin->major_version = rz_buf_read_be16(buf);

	bin->constant_pool_count = rz_buf_read_be16(buf);
	bin->constant_pool_count = sanitize_size(buffer_size - rz_buf_tell(buf), bin->constant_pool_count, 3);
	bin->constant_pool_offset = base + rz_buf_tell(buf);

	if (bin->constant_pool_count > 0) {
		bin->constant_pool = RZ_NEWS0(ConstPool *, bin->constant_pool_count);
		if (!bin->constant_pool) {
			goto java_class_parse_bad;
		}
		for (ut32 i = 1; i < bin->constant_pool_count; ++i) {
			offset = rz_buf_tell(buf) + base;
			ConstPool *cpool = java_constant_pool_new(buf, offset);
			if (!cpool) {
				rz_warn_if_reached();
				break;
			}
			bin->constant_pool[i] = cpool;
			if (java_constant_pool_requires_null(cpool)) {
				i++;
				bin->constant_pool[i] = java_constant_null_new(offset);
			}
		}
	}
	bin->access_flags = rz_buf_read_be16(buf);
	bin->this_class = rz_buf_read_be16(buf);
	bin->super_class = rz_buf_read_be16(buf);

	bin->interfaces_count = rz_buf_read_be16(buf);
	bin->interfaces_count = sanitize_size(buffer_size - rz_buf_tell(buf), bin->interfaces_count, 2);
	bin->interfaces_offset = base + rz_buf_tell(buf);

	if (bin->interfaces_count > 0) {
		bin->interfaces = RZ_NEWS0(Interface *, bin->interfaces_count);
		if (!bin->interfaces) {
			goto java_class_parse_bad;
		}
		for (ut32 i = 0; i < bin->interfaces_count; ++i) {
			offset = rz_buf_tell(buf) + base;
			bin->interfaces[i] = java_interface_new(buf, offset);
		}
	}

	bin->fields_count = rz_buf_read_be16(buf);
	bin->fields_count = sanitize_size(buffer_size - rz_buf_tell(buf), bin->fields_count, 8);
	bin->fields_offset = base + rz_buf_tell(buf);

	if (bin->fields_count > 0) {
		bin->fields = RZ_NEWS0(Field *, bin->fields_count);
		if (!bin->fields) {
			goto java_class_parse_bad;
		}
		for (ut32 i = 0; i < bin->fields_count; ++i) {
			offset = rz_buf_tell(buf) + base;
			bin->fields[i] = java_field_new(bin->constant_pool,
				bin->constant_pool_count, buf, offset);
		}
	}

	bin->methods_count = rz_buf_read_be16(buf);
	bin->methods_count = sanitize_size(buffer_size - rz_buf_tell(buf), bin->methods_count, 8);
	bin->methods_offset = base + rz_buf_tell(buf);

	if (bin->methods_count > 0) {
		bin->methods = RZ_NEWS0(Method *, bin->methods_count);
		if (!bin->methods) {
			goto java_class_parse_bad;
		}
		for (ut32 i = 0; i < bin->methods_count; ++i) {
			offset = rz_buf_tell(buf) + base;
			bin->methods[i] = java_method_new(bin->constant_pool,
				bin->constant_pool_count, buf, offset);
		}
	}

	bin->attributes_count = rz_buf_read_be16(buf);
	bin->attributes_count = sanitize_size(buffer_size - rz_buf_tell(buf), bin->attributes_count, 6);
	bin->attributes_offset = base + rz_buf_tell(buf);

	if (bin->attributes_count > 0) {
		bin->attributes = RZ_NEWS0(Attribute *, bin->attributes_count);
		if (!bin->attributes) {
			goto java_class_parse_bad;
		}
		for (ut32 i = 0; i < bin->attributes_count; ++i) {
			offset = rz_buf_tell(buf) + base;
			Attribute *attr = java_attribute_new(buf, offset);
			if (attr && java_attribute_resolve(bin->constant_pool, bin->constant_pool_count, attr, buf)) {
				bin->attributes[i] = attr;
			} else {
				java_attribute_free(attr);
			}
		}
	}
	bin->class_end_offset = base + rz_buf_tell(buf);
	if (size) {
		*size = rz_buf_tell(buf);
	}
	return true;

java_class_parse_bad:
	rz_bin_java_class_free(bin);
	return false;
}

static void java_set_sdb(Sdb *kv, RzBinJavaClass *bin, ut64 offset, ut64 size) {
	char *tmp_val;
	char tmp_key[256];

	sdb_num_set(kv, "java_class.offset", offset, 0);
	sdb_num_set(kv, "java_class.size", size, 0);
	sdb_num_set(kv, "java_class.magic", size, 0);
	sdb_num_set(kv, "java_class.minor_version", size, 0);
	sdb_num_set(kv, "java_class.major_version", size, 0);

	tmp_val = rz_bin_java_class_version(bin);
	if (tmp_val) {
		sdb_set(kv, "java_class.version", tmp_val, 0);
		free(tmp_val);
	}

	sdb_num_set(kv, "java_class.constant_pool_count", bin->constant_pool_count, 0);
	for (ut32 i = 0; i < bin->constant_pool_count; ++i) {
		ConstPool *cpool = bin->constant_pool[i];
		if (!cpool) {
			continue;
		}
		tmp_val = java_constant_pool_stringify(cpool);
		if (tmp_val) {
			snprintf(tmp_key, sizeof(tmp_key), "java_class.constant_pool_%d", i);
			sdb_set(kv, tmp_key, tmp_val, 0);
		}
	}

	sdb_num_set(kv, "java_class.fields_count", bin->fields_count, 0);
	sdb_num_set(kv, "java_class.methods_count", bin->methods_count, 0);
	sdb_num_set(kv, "java_class.attributes_count", bin->attributes_count, 0);
}

RZ_API RzBinJavaClass *rz_bin_java_class_new(RzBuffer *buf, ut64 offset, Sdb *kv) {
	RzBinJavaClass *bin = (RzBinJavaClass *)RZ_NEW0(RzBinJavaClass);
	rz_return_val_if_fail(bin, NULL);

	ut64 size;
	if (!java_class_parse(bin, offset, kv, buf, &size)) {
		return NULL;
	}

	java_set_sdb(kv, bin, offset, size);

	return bin;
}

RZ_API char *rz_bin_java_class_version(RzBinJavaClass *bin) {
	if (!bin) {
		return NULL;
	}
#define is_version(bin, major, minor) ((bin)->major_version == (major) && (bin)->minor_version >= (minor))
	if (is_version(bin, 45, 3)) {
		return strdup("Java SE base (< 1.5)"); // base level for all attributes
	} else if (is_version(bin, 49, 0)) {
		return strdup("Java SE 1.5"); // Java SE 1.5: enum, generics, annotations
	} else if (is_version(bin, 50, 0)) {
		return strdup("Java SE 1.6"); // Java SE 1.6: stackmaps
	} else if (is_version(bin, 51, 0)) {
		return strdup("Java SE 1.7"); // Java SE 1.7
	} else if (is_version(bin, 52, 0)) {
		return strdup("Java SE 1.8"); // Java SE 1.8: lambda, type annos, param names
	} else if (is_version(bin, 53, 0)) {
		return strdup("Java SE 1.9"); // Java SE 1.9: modules, indy string concat
	} else if (is_version(bin, 54, 0)) {
		return strdup("Java SE 10"); // Java SE 10
	} else if (is_version(bin, 55, 0)) {
		return strdup("Java SE 11"); // Java SE 11: constant dynamic, nest mates
	} else if (is_version(bin, 56, 0)) {
		return strdup("Java SE 12"); // Java SE 12
	} else if (is_version(bin, 57, 0)) {
		return strdup("Java SE 13"); // Java SE 13
	} else if (is_version(bin, 58, 0)) {
		return strdup("Java SE 14"); // Java SE 14
	} else if (is_version(bin, 59, 0)) {
		return strdup("Java SE 15"); // Java SE 15
	} else if (is_version(bin, 60, 0)) {
		return strdup("Java SE 16"); // Java SE 16
	}
#undef is_version
	return strdup("unknown");
}

RZ_API ut64 rz_bin_java_class_debug_info(RzBinJavaClass *bin) {
	if (!bin) {
		return 0;
	}
	if (bin->methods) {
		for (ut32 i = 0; i < bin->methods_count; ++i) {
			Method *method = bin->methods[i];
			if (!method || method->attributes_count < 1) {
				continue;
			}
			for (ut32 k = 0; k < method->attributes_count; ++k) {
				Attribute *attr = method->attributes[k];
				if (attr && attr->type == ATTRIBUTE_TYPE_CODE) {
					AttributeCode *ac = (AttributeCode *)attr->info;
					for (ut32 k = 0; k < ac->attributes_count; k++) {
						Attribute *cattr = ac->attributes[k];
						if (cattr && cattr->type == ATTRIBUTE_TYPE_LINENUMBERTABLE) {
							return RZ_BIN_DBG_LINENUMS | RZ_BIN_DBG_SYMS;
						}
					}
				}
			}
		}
	}
	return RZ_BIN_DBG_SYMS;
}

RZ_API char *rz_bin_java_class_language(RzBinJavaClass *bin) {
	rz_return_val_if_fail(bin, NULL);
	const char *language = "java";
	char *string = NULL;
	if (bin->constant_pool) {
		for (ut32 i = 0; i < bin->constant_pool_count; ++i) {
			const ConstPool *cpool = bin->constant_pool[i];
			if (!cpool || !java_constant_pool_is_string(cpool)) {
				continue;
			}
			char *string = java_constant_pool_stringify(cpool);
			if (!strncmp(string, "kotlin/jvm", 10)) {
				language = "kotlin";
				break;
			} else if (!strncmp(string, "org/codehaus/groovy/runtime", 27)) {
				language = "groovy";
				break;
			}
			free(string);
			string = NULL;
		}
	}
	free(string);
	return strdup(language);
}

RZ_API void rz_bin_java_class_free(RzBinJavaClass *bin) {
	if (!bin) {
		return;
	}
	if (bin->constant_pool) {
		for (ut32 i = 0; i < bin->constant_pool_count; ++i) {
			java_constant_pool_free(bin->constant_pool[i]);
		}
		free(bin->constant_pool);
	}
	if (bin->interfaces) {
		for (ut32 i = 0; i < bin->interfaces_count; ++i) {
			java_interface_free(bin->interfaces[i]);
		}
		free(bin->interfaces);
	}
	if (bin->fields) {
		for (ut32 i = 0; i < bin->fields_count; ++i) {
			java_field_free(bin->fields[i]);
		}
		free(bin->fields);
	}
	if (bin->methods) {
		for (ut32 i = 0; i < bin->methods_count; ++i) {
			java_method_free(bin->methods[i]);
		}
		free(bin->methods);
	}
	if (bin->attributes) {
		for (ut32 i = 0; i < bin->attributes_count; ++i) {
			java_attribute_free(bin->attributes[i]);
		}
		free(bin->attributes);
	}
	free(bin);
}

RZ_API char *rz_bin_java_class_name(RzBinJavaClass *bin) {
	ut16 index;
	rz_return_val_if_fail(bin, NULL);
	const ConstPool *cpool = java_class_constant_pool_at(bin, bin->this_class);

	if (!cpool || java_constant_pool_resolve(cpool, &index, NULL) != 1) {
		rz_warn_if_reached();
		return strdup("unknown_class");
	}

	return java_class_constant_pool_stringify_at(bin, index);
}

RZ_API char *rz_bin_java_class_super(RzBinJavaClass *bin) {
	ut16 index;
	rz_return_val_if_fail(bin, NULL);
	const ConstPool *cpool = java_class_constant_pool_at(bin, bin->super_class);
	if (!cpool || java_constant_pool_resolve(cpool, &index, NULL) != 1) {
		rz_warn_if_reached();
		return strdup("unknown_super");
	}
	return java_class_constant_pool_stringify_at(bin, index);
}

RZ_API ut32 rz_bin_java_class_access_flags(RzBinJavaClass *bin) {
	rz_return_val_if_fail(bin, 0xffffffff);
	return bin->access_flags;
}

RZ_API char *rz_bin_java_class_access_flags_readable(RzBinJavaClass *bin) {
	rz_return_val_if_fail(bin, NULL);
	RzStrBuf *sb = NULL;

	for (ut32 i = 0; i < CLASS_ACCESS_FLAGS_SIZE; ++i) {
		const AccessFlagsReadable *afr = &access_flags_list[i];
		if (bin->access_flags & afr->flag) {
			if (!sb) {
				sb = rz_strbuf_new(afr->readable);
				if (!sb) {
					return NULL;
				}
			} else {
				rz_strbuf_appendf(sb, " %s", afr->readable);
			}
		}
	}

	return sb ? rz_strbuf_drain(sb) : NULL;
}

static int calculate_padding_ut16(ut16 count) {
	if (count > 9999) {
		return 5;
	} else if (count > 999) {
		return 4;
	} else if (count > 99) {
		return 3;
	}
	return 2;
}

RZ_API void rz_bin_java_class_as_json(RzBinJavaClass *bin, PJ *j) {
	rz_return_if_fail(bin && j);
	char *tmp = NULL;

	pj_o(j);

	pj_ko(j, "version");
	{
		pj_kn(j, "minor", bin->minor_version);
		pj_kn(j, "major", bin->major_version);
		tmp = rz_bin_java_class_version(bin);
		pj_ks(j, "version", tmp ? tmp : "");
		free(tmp);
	}
	pj_end(j);

	pj_kn(j, "constant_pool_count", bin->constant_pool_count);
	pj_k(j, "constant_pool");
	rz_bin_java_class_const_pool_as_json(bin, j);

	pj_kn(j, "access_flags_n", bin->access_flags);
	tmp = rz_bin_java_class_access_flags_readable(bin);
	pj_ks(j, "access_flags_s", tmp ? tmp : "");
	free(tmp);

	pj_kn(j, "class_n", bin->this_class);
	tmp = rz_bin_java_class_name(bin);
	pj_ks(j, "class_s", tmp ? tmp : "");
	free(tmp);

	pj_kn(j, "super_n", bin->super_class);
	tmp = rz_bin_java_class_super(bin);
	pj_ks(j, "super_s", tmp ? tmp : "");
	free(tmp);

	pj_kn(j, "interfaces_count", bin->interfaces_count);
	pj_k(j, "interfaces");
	rz_bin_java_class_interfaces_as_json(bin, j);

	pj_kn(j, "methods_count", bin->methods_count);
	pj_k(j, "methods");
	rz_bin_java_class_methods_as_json(bin, j);

	pj_kn(j, "fields_count", bin->fields_count);
	pj_k(j, "fields");
	rz_bin_java_class_fields_as_json(bin, j);

	pj_kn(j, "attributes_count", bin->attributes_count);
	pj_ka(j, "attributes");
	for (ut32 i = 0; i < bin->attributes_count; ++i) {
		Attribute *attr = bin->attributes[i];
		if (!attr) {
			continue;
		}
		pj_o(j);
		pj_kn(j, "offset", attr->offset);
		pj_kn(j, "size", attr->attribute_length);
		pj_kn(j, "name_n", attr->attribute_name_index);
		tmp = java_class_constant_pool_stringify_at(bin, attr->attribute_name_index);
		pj_ks(j, "name_s", tmp ? tmp : "");
		free(tmp);
		pj_end(j);
	}
	pj_end(j);
	pj_end(j);
}

RZ_API void rz_bin_java_class_as_text(RzBinJavaClass *bin, RzStrBuf *sb) {
	rz_return_if_fail(bin && sb);
	char number[16];
	char *tmp = NULL;
	int padding;

	tmp = rz_bin_java_class_version(bin);
	rz_strbuf_appendf(sb, "Version: (%u.%u) %s\n", bin->major_version, bin->minor_version, tmp);
	free(tmp);

	tmp = rz_bin_java_class_access_flags_readable(bin);
	rz_strbuf_appendf(sb, "Flags: (0x%04x) %s\n", bin->access_flags, tmp);
	free(tmp);

	tmp = rz_bin_java_class_name(bin);
	rz_strbuf_appendf(sb, "Class: (#%u) %s\n", bin->this_class, tmp);
	free(tmp);

	tmp = rz_bin_java_class_super(bin);
	rz_strbuf_appendf(sb, "Super: (#%u) %s\n", bin->super_class, tmp);
	free(tmp);

	rz_bin_java_class_const_pool_as_text(bin, sb);
	rz_bin_java_class_interfaces_as_text(bin, sb);
	rz_bin_java_class_methods_as_text(bin, sb);
	rz_bin_java_class_fields_as_text(bin, sb);

	rz_strbuf_appendf(sb, "Attributes: %u\n", bin->attributes_count);
	padding = calculate_padding_ut16(bin->attributes_count) + 1;
	for (ut32 i = 0; i < bin->attributes_count; ++i) {
		Attribute *attr = bin->attributes[i];
		if (!attr) {
			continue;
		}
		snprintf(number, sizeof(number), "#%u", i);
		tmp = java_class_constant_pool_stringify_at(bin, attr->attribute_name_index);
		rz_strbuf_appendf(sb, "  %-*s = #%-5u size: %-5u %s\n", padding, number, attr->attribute_name_index, attr->attribute_length, tmp);
		free(tmp);
	}
}

RZ_API RzBinAddr *rz_bin_java_class_resolve_symbol(RzBinJavaClass *bin, int resolve) {
	RzBinAddr *ret = RZ_NEW0(RzBinAddr);
	if (!ret) {
		return NULL;
	}
	ret->paddr = UT64_MAX;

	char *name = NULL;
	if (bin->methods) {
		for (ut32 i = 0; i < bin->methods_count; ++i) {
			const Method *method = bin->methods[i];
			if (!method) {
				continue;
			}

			name = java_class_constant_pool_stringify_at(bin, method->name_index);
			if (!name) {
				continue;
			}

			if (resolve == RZ_BIN_SYM_ENTRY || resolve == RZ_BIN_SYM_INIT) {
				if (strcmp(name, "<init>") != 0 && strcmp(name, "<clinit>") != 0) {
					free(name);
					continue;
				}
			} else if (resolve == RZ_BIN_SYM_MAIN) {
				if (strcmp(name, "main") != 0) {
					free(name);
					continue;
				}
			}
			free(name);
			ut64 addr = UT64_MAX;
			for (ut32 i = 0; i < method->attributes_count; ++i) {
				Attribute *attr = method->attributes[i];
				if (attr && attr->type == ATTRIBUTE_TYPE_CODE) {
					AttributeCode *ac = (AttributeCode *)attr->info;
					addr = ac->code_offset;
					break;
				}
			}
			if (addr == UT64_MAX) {
				rz_warn_if_reached();
				continue;
			}
			ret->paddr = addr;
			break;
		}
	}
	return ret;
}

RZ_API RzList *rz_bin_java_class_entrypoints(RzBinJavaClass *bin) {
	rz_return_val_if_fail(bin, NULL);

	RzList *list = rz_list_newf(free);
	if (!list) {
		return NULL;
	}

	char *name = NULL;
	bool is_static;
	if (bin->methods) {
		for (ut32 i = 0; i < bin->methods_count; ++i) {
			const Method *method = bin->methods[i];
			if (!method) {
				rz_warn_if_reached();
				continue;
			}
			is_static = method->access_flags & METHOD_ACCESS_FLAG_STATIC;
			if (!is_static) {
				name = java_class_constant_pool_stringify_at(bin, method->name_index);
				if (!name) {
					continue;
				}
				if (strcmp(name, "main") != 0 && strcmp(name, "<init>") != 0 && strcmp(name, "<clinit>") != 0) {
					free(name);
					continue;
				}
				free(name);
			}

			ut64 addr = UT64_MAX;
			for (ut32 i = 0; i < method->attributes_count; ++i) {
				Attribute *attr = method->attributes[i];
				if (attr && attr->type == ATTRIBUTE_TYPE_CODE) {
					AttributeCode *ac = (AttributeCode *)attr->info;
					addr = ac->code_offset;
					break;
				}
			}
			if (addr == UT64_MAX) {
				rz_warn_if_reached();
				continue;
			}

			RzBinAddr *entrypoint = RZ_NEW0(RzBinAddr);
			if (!entrypoint) {
				rz_warn_if_reached();
				continue;
			}
			entrypoint->vaddr = entrypoint->paddr = addr;
			rz_list_append(list, entrypoint);
		}
	}
	return list;
}

RZ_API RzList *rz_bin_java_class_strings(RzBinJavaClass *bin) {
	rz_return_val_if_fail(bin, NULL);

	RzList *list = rz_list_newf(rz_bin_string_free);
	if (!list) {
		return NULL;
	}

	char *string;
	if (bin->constant_pool_count > 0) {
		for (ut32 i = 1; i < bin->constant_pool_count; ++i) {
			const ConstPool *cpool = bin->constant_pool[i];
			if (!cpool || !java_constant_pool_is_string(cpool) || cpool->size < 1) {
				continue;
			}
			string = java_constant_pool_stringify(cpool);
			if (!string) {
				rz_warn_if_reached();
				continue;
			}
			RzBinString *bstr = RZ_NEW0(RzBinString);
			if (!bstr) {
				free(string);
				rz_warn_if_reached();
				continue;
			}
			bstr->paddr = cpool->offset;
			bstr->ordinal = i;
			bstr->length = cpool->size;
			bstr->size = cpool->size;
			bstr->string = string;
			bstr->type = RZ_STRING_TYPE_UTF8;
			rz_list_append(list, bstr);
		}
	}
	return list;
}

static char *add_class_name_to_name(char *name, char *classname) {
	char *tmp;
	if (classname && name) {
		tmp = rz_str_newf("%s.%s", classname, name);
		if (!tmp) {
			return name;
		}
		free(name);
		rz_str_replace_char(tmp, '/', '.');
		return tmp;
	}
	return name;
}

RZ_API RzList *rz_bin_java_class_methods_as_symbols(RzBinJavaClass *bin) {
	rz_return_val_if_fail(bin, NULL);

	RzList *list = rz_list_newf(rz_bin_symbol_free);
	if (!list) {
		return NULL;
	}

	char *sym = NULL;
	if (bin->methods) {
		for (ut32 i = 0; i < bin->methods_count; ++i) {
			const Method *method = bin->methods[i];
			if (!method) {
				rz_warn_if_reached();
				continue;
			}
			const ConstPool *cpool = java_class_constant_pool_at(bin, method->name_index);
			if (!cpool || !java_constant_pool_is_string(cpool)) {
				rz_warn_if_reached();
				continue;
			}
			sym = java_constant_pool_stringify(cpool);
			if (!sym) {
				continue;
			}
			ut64 size = 0;
			ut64 addr = UT64_MAX;
			for (ut32 i = 0; i < method->attributes_count; ++i) {
				Attribute *attr = method->attributes[i];
				if (attr && attr->type == ATTRIBUTE_TYPE_CODE) {
					AttributeCode *ac = (AttributeCode *)attr->info;
					addr = ac->code_offset;
					size = attr->attribute_length;
					break;
				}
			}
			RzBinSymbol *symbol = rz_bin_symbol_new(NULL, addr, addr);
			if (!symbol) {
				rz_warn_if_reached();
				free(sym);
				continue;
			}
			symbol->classname = rz_bin_java_class_name(bin);
			symbol->name = add_class_name_to_name(sym, symbol->classname);
			symbol->size = size;
			symbol->bind = java_method_is_global(method) ? RZ_BIN_BIND_GLOBAL_STR : RZ_BIN_BIND_LOCAL_STR;
			symbol->type = RZ_BIN_TYPE_FUNC_STR;
			symbol->ordinal = rz_list_length(list);
			symbol->visibility = method->access_flags;
			symbol->visibility_str = java_method_access_flags_readable(method);
			rz_list_append(list, symbol);
		}
	}
	return list;
}

RZ_API void rz_bin_java_class_methods_as_text(RzBinJavaClass *bin, RzStrBuf *sb) {
	rz_return_if_fail(bin && sb);

	rz_strbuf_appendf(sb, "Methods: %u\n", bin->methods_count);
	char number[16];
	char *flags, *name, *descr;
	if (bin->methods) {
		for (ut32 i = 0; i < bin->methods_count; ++i) {
			const Method *method = bin->methods[i];
			if (!method) {
				rz_warn_if_reached();
				continue;
			}
			flags = java_method_access_flags_readable(method);
			name = java_class_constant_pool_stringify_at(bin, method->name_index);
			descr = java_class_constant_pool_stringify_at(bin, method->descriptor_index);

			if (flags) {
				rz_strbuf_appendf(sb, "  %s %s%s;\n", flags, name, descr);
			} else {
				rz_strbuf_appendf(sb, "  %s%s;\n", name, descr);
			}
			rz_strbuf_appendf(sb, "    name: %s\n", name);
			rz_strbuf_appendf(sb, "    descriptor: %s\n", descr);
			rz_strbuf_appendf(sb, "    flags: (0x%04x) %s\n", method->access_flags, flags ? flags : "");

			free(flags);
			free(name);
			free(descr);
			rz_strbuf_appendf(sb, "    attributes: %u\n", method->attributes_count);
			int padding = calculate_padding_ut16(method->attributes_count) + 1;
			for (ut32 i = 0; i < method->attributes_count; ++i) {
				Attribute *attr = method->attributes[i];
				if (!attr) {
					continue;
				}
				snprintf(number, sizeof(number), "#%u", i);
				name = java_class_constant_pool_stringify_at(bin, attr->attribute_name_index);
				rz_strbuf_appendf(sb, "      %-*s = #%-5u size: %-5u %s\n", padding, number, attr->attribute_name_index, attr->attribute_length, name);
				free(name);
			}
		}
	}
}

RZ_API void rz_bin_java_class_methods_as_json(RzBinJavaClass *bin, PJ *j) {
	rz_return_if_fail(bin && j);

	pj_a(j);

	char *tmp;
	if (bin->methods) {
		for (ut32 i = 0; i < bin->methods_count; ++i) {
			const Method *method = bin->methods[i];
			if (!method) {
				rz_warn_if_reached();
				continue;
			}
			pj_o(j); // {
			pj_kn(j, "offset", method->offset);

			pj_kn(j, "access_flags_n", method->access_flags);
			tmp = java_method_access_flags_readable(method);
			pj_ks(j, "access_flags_s", tmp ? tmp : "");
			free(tmp);

			pj_kn(j, "name_n", method->name_index);
			tmp = java_class_constant_pool_stringify_at(bin, method->name_index);
			pj_ks(j, "name_s", tmp ? tmp : "");
			free(tmp);

			pj_kn(j, "descriptor_n", method->descriptor_index);
			tmp = java_class_constant_pool_stringify_at(bin, method->descriptor_index);
			pj_ks(j, "descriptor_s", tmp ? tmp : "");
			free(tmp);

			pj_kn(j, "attributes_count", method->attributes_count);
			pj_ka(j, "attributes"); // [
			for (ut32 i = 0; i < method->attributes_count; ++i) {
				Attribute *attr = method->attributes[i];
				if (!attr) {
					rz_warn_if_reached();
					continue;
				}
				pj_o(j);
				pj_kn(j, "offset", attr->offset);
				pj_kn(j, "size", attr->attribute_length);
				pj_kn(j, "name_n", attr->attribute_name_index);
				tmp = java_class_constant_pool_stringify_at(bin, attr->attribute_name_index);
				pj_ks(j, "name_s", tmp ? tmp : "");
				free(tmp);
				pj_end(j);
			}
			pj_end(j); // ]
			pj_end(j); // }
		}
	}
	pj_end(j);
}

RZ_API RzList *rz_bin_java_class_fields_as_symbols(RzBinJavaClass *bin) {
	rz_return_val_if_fail(bin, NULL);

	RzList *list = rz_list_newf(rz_bin_symbol_free);
	if (!list) {
		return NULL;
	}

	char *sym = NULL;
	if (bin->fields) {
		for (ut32 i = 0; i < bin->fields_count; ++i) {
			const Field *field = bin->fields[i];
			if (!field) {
				rz_warn_if_reached();
				continue;
			}
			const ConstPool *cpool = java_class_constant_pool_at(bin, field->name_index);
			if (!cpool || !java_constant_pool_is_string(cpool)) {
				rz_warn_if_reached();
				continue;
			}
			sym = java_constant_pool_stringify(cpool);
			if (!sym) {
				continue;
			}
			RzBinSymbol *symbol = rz_bin_symbol_new(NULL, field->offset, field->offset);
			if (!symbol) {
				rz_warn_if_reached();
				free(sym);
				continue;
			}
			symbol->classname = rz_bin_java_class_name(bin);
			symbol->name = add_class_name_to_name(sym, symbol->classname);
			symbol->size = 0;
			symbol->bind = java_field_is_global(field) ? RZ_BIN_BIND_GLOBAL_STR : RZ_BIN_BIND_LOCAL_STR;
			symbol->type = RZ_BIN_TYPE_OBJECT_STR;
			symbol->ordinal = i;
			symbol->visibility = field->access_flags;
			symbol->visibility_str = java_field_access_flags_readable(field);
			rz_list_append(list, symbol);
		}
	}
	return list;
}

RZ_API RzList *rz_bin_java_class_fields_as_binfields(RzBinJavaClass *bin) {
	rz_return_val_if_fail(bin, NULL);

	RzList *list = rz_list_newf(rz_bin_field_free);
	if (!list) {
		return NULL;
	}

	char *name = NULL;
	if (bin->fields) {
		for (ut32 i = 0; i < bin->fields_count; ++i) {
			const Field *field = bin->fields[i];
			if (!field) {
				rz_warn_if_reached();
				continue;
			}
			const ConstPool *cpool = java_class_constant_pool_at(bin, field->name_index);
			if (!cpool || !java_constant_pool_is_string(cpool)) {
				rz_warn_if_reached();
				continue;
			}
			name = java_constant_pool_stringify(cpool);
			if (!name) {
				continue;
			}
			RzBinField *bf = rz_bin_field_new(field->offset, field->offset, 0, name, NULL, NULL, false);
			if (bf) {
				bf->visibility = field->access_flags;
				bf->type = java_class_constant_pool_stringify_at(bin, field->descriptor_index);
				rz_list_append(list, bf);
			}
			free(name);
		}
	}
	return list;
}

RZ_API void rz_bin_java_class_fields_as_text(RzBinJavaClass *bin, RzStrBuf *sb) {
	rz_return_if_fail(bin && sb);

	rz_strbuf_appendf(sb, "Fields: %u\n", bin->fields_count);
	char number[16];
	char *flags, *name, *descr;
	if (bin->fields) {
		for (ut32 i = 0; i < bin->fields_count; ++i) {
			const Field *field = bin->fields[i];
			if (!field) {
				rz_warn_if_reached();
				continue;
			}
			flags = java_field_access_flags_readable(field);
			name = java_class_constant_pool_stringify_at(bin, field->name_index);
			descr = java_class_constant_pool_stringify_at(bin, field->descriptor_index);

			if (flags) {
				rz_strbuf_appendf(sb, "  %s %s%s;\n", flags, name, descr);
			} else {
				rz_strbuf_appendf(sb, "  %s%s;\n", name, descr);
			}
			rz_strbuf_appendf(sb, "    name: %s\n", name);
			rz_strbuf_appendf(sb, "    descriptor: %s\n", descr);
			rz_strbuf_appendf(sb, "    flags: (0x%04x) %s\n", field->access_flags, flags);

			free(flags);
			free(name);
			free(descr);
			rz_strbuf_appendf(sb, "    attributes: %u\n", field->attributes_count);
			int padding = calculate_padding_ut16(field->attributes_count) + 1;
			for (ut32 i = 0; i < field->attributes_count; ++i) {
				Attribute *attr = field->attributes[i];
				if (!attr) {
					continue;
				}
				snprintf(number, sizeof(number), "#%u", i);
				name = java_class_constant_pool_stringify_at(bin, attr->attribute_name_index);
				rz_strbuf_appendf(sb, "      %*s = #%-5u size: %-5u %s\n", padding, number, attr->attribute_name_index, attr->attribute_length, name);
				free(name);
			}
		}
	}
}

RZ_API void rz_bin_java_class_fields_as_json(RzBinJavaClass *bin, PJ *j) {
	rz_return_if_fail(bin && j);

	pj_a(j);

	char *tmp;
	if (bin->fields) {
		for (ut32 i = 0; i < bin->fields_count; ++i) {
			const Field *field = bin->fields[i];
			if (!field) {
				rz_warn_if_reached();
				continue;
			}
			pj_o(j); // {

			pj_kn(j, "offset", field->offset);

			pj_kn(j, "access_flags_n", field->access_flags);
			tmp = java_field_access_flags_readable(field);
			pj_ks(j, "access_flags_s", tmp ? tmp : "");
			free(tmp);

			pj_kn(j, "name_n", field->name_index);
			tmp = java_class_constant_pool_stringify_at(bin, field->name_index);
			pj_ks(j, "name_s", tmp ? tmp : "");
			free(tmp);

			pj_kn(j, "descriptor_n", field->descriptor_index);
			tmp = java_class_constant_pool_stringify_at(bin, field->descriptor_index);
			pj_ks(j, "descriptor_s", tmp ? tmp : "");
			free(tmp);

			pj_kn(j, "attributes_count", field->attributes_count);
			pj_ka(j, "attributes"); // [
			for (ut32 i = 0; i < field->attributes_count; ++i) {
				Attribute *attr = field->attributes[i];
				if (!attr) {
					continue;
				}
				pj_o(j);
				pj_kn(j, "offset", attr->offset);
				pj_kn(j, "size", attr->attribute_length);
				pj_kn(j, "name_n", attr->attribute_name_index);
				tmp = java_class_constant_pool_stringify_at(bin, attr->attribute_name_index);
				pj_ks(j, "name_s", tmp ? tmp : "");
				free(tmp);
				pj_end(j);
			}
			pj_end(j); // ]
			pj_end(j); // }
		}
	}
	pj_end(j);
}

static char *import_type(const ConstPool *cpool) {
	if (cpool->tag == CONSTANT_POOL_METHODREF) {
		return RZ_BIN_TYPE_METH_STR;
	} else if (cpool->tag == CONSTANT_POOL_FIELDREF) {
		return "FIELD";
	} else if (cpool->tag == CONSTANT_POOL_INTERFACEMETHODREF) {
		return "IMETH";
	}
	return RZ_BIN_TYPE_UNKNOWN_STR;
}

RZ_API RzList *rz_bin_java_class_const_pool_as_symbols(RzBinJavaClass *bin) {
	rz_return_val_if_fail(bin, NULL);

	RzList *list = rz_list_newf(rz_bin_symbol_free);
	if (!list) {
		return NULL;
	}
	char *sym, *classname;
	bool is_main;
	ut16 class_index, name_and_type_index, name_index, descriptor_index, class_name_index;
	if (bin->constant_pool) {
		for (ut32 i = 0; i < bin->constant_pool_count; ++i) {
			const ConstPool *cpool = bin->constant_pool[i];
			if (!cpool || !java_constant_pool_is_import(cpool)) {
				continue;
			}
			if (java_constant_pool_resolve(cpool, &class_index, &name_and_type_index) != 2) {
				rz_warn_if_reached();
				continue;
			}
			const ConstPool *nat = java_class_constant_pool_at(bin, name_and_type_index);
			if (!nat ||
				java_constant_pool_resolve(nat, &name_index, &descriptor_index) != 2) {
				rz_warn_if_reached();
				continue;
			}
			const ConstPool *pclass = java_class_constant_pool_at(bin, class_index);
			if (!pclass ||
				java_constant_pool_resolve(pclass, &class_name_index, NULL) != 1) {
				rz_warn_if_reached();
				continue;
			}
			RzBinSymbol *symbol = rz_bin_symbol_new(NULL, cpool->offset, cpool->offset);
			if (!symbol) {
				rz_warn_if_reached();
				free(sym);
				continue;
			}
			sym = java_class_constant_pool_stringify_at(bin, name_index);
			is_main = sym && !strcmp(sym, "main");
			classname = java_class_constant_pool_stringify_at(bin, class_name_index);
			symbol->name = add_class_name_to_name(sym, classname);
			symbol->classname = classname;
			symbol->bind = RZ_BIN_BIND_IMPORT_STR;
			symbol->type = is_main ? RZ_BIN_TYPE_FUNC_STR : import_type(cpool);
			symbol->ordinal = i;
			symbol->is_imported = true;
			rz_list_append(list, symbol);
		}
	}

	return list;
}

RZ_API RzList *rz_bin_java_class_const_pool_as_imports(RzBinJavaClass *bin) {
	rz_return_val_if_fail(bin, NULL);

	RzList *imports = rz_list_newf(rz_bin_import_free);
	if (!imports) {
		return NULL;
	}
	bool is_main;
	ut16 class_index, name_and_type_index, name_index, descriptor_index, class_name_index;
	if (bin->constant_pool) {
		for (ut32 i = 0; i < bin->constant_pool_count; ++i) {
			const ConstPool *cpool = bin->constant_pool[i];
			if (!cpool || !java_constant_pool_is_import(cpool)) {
				continue;
			}
			if (java_constant_pool_resolve(cpool, &class_index, &name_and_type_index) != 2) {
				rz_warn_if_reached();
				continue;
			}
			const ConstPool *nat = java_class_constant_pool_at(bin, name_and_type_index);
			if (!nat ||
				java_constant_pool_resolve(nat, &name_index, &descriptor_index) != 2) {
				rz_warn_if_reached();
				continue;
			}
			const ConstPool *pclass = java_class_constant_pool_at(bin, class_index);
			if (!pclass ||
				java_constant_pool_resolve(pclass, &class_name_index, NULL) != 1) {
				rz_warn_if_reached();
				continue;
			}

			RzBinImport *import = RZ_NEW0(RzBinImport);
			if (!import) {
				rz_warn_if_reached();
				continue;
			}
			import->classname = java_class_constant_pool_stringify_at(bin, class_name_index);
			rz_str_replace_char(import->classname, '/', '.');
			import->name = java_class_constant_pool_stringify_at(bin, name_index);
			is_main = import->name && !strcmp(import->name, "main");
			import->bind = is_main ? RZ_BIN_BIND_GLOBAL_STR : NULL;
			import->type = is_main ? RZ_BIN_TYPE_FUNC_STR : import_type(cpool);
			import->descriptor = java_class_constant_pool_stringify_at(bin, descriptor_index);
			import->ordinal = i;
			rz_list_append(imports, import);
		}
	}

	if (bin->interfaces) {
		for (ut32 i = 0; i < bin->interfaces_count; ++i) {
			if (!bin->interfaces[i]) {
				continue;
			}

			RzBinImport *import = RZ_NEW0(RzBinImport);
			if (!import) {
				rz_warn_if_reached();
				continue;
			}
			const ConstPool *cpool = java_class_constant_pool_at(bin, bin->interfaces[i]->index);
			if (!cpool || java_constant_pool_resolve(cpool, &class_index, NULL) != 1) {
				rz_warn_if_reached();
				continue;
			}

			import->classname = java_class_constant_pool_stringify_at(bin, class_index);
			rz_str_replace_char(import->classname, '/', '.');
			import->name = strdup("*");
			import->bind = RZ_BIN_BIND_WEAK_STR;
			import->type = RZ_BIN_TYPE_IFACE_STR;
			import->ordinal = i;
			rz_list_append(imports, import);
		}
	}

	return imports;
}

RZ_API void rz_bin_java_class_const_pool_as_text(RzBinJavaClass *bin, RzStrBuf *sb) {
	rz_return_if_fail(bin && sb);

	char number[16];
	const char *tag;
	char *text;
	rz_strbuf_appendf(sb, "Constant pool: %u\n", bin->constant_pool_count);
	if (bin->constant_pool) {
		int padding = calculate_padding_ut16(bin->constant_pool_count) + 1;
		for (ut32 i = 0; i < bin->constant_pool_count; ++i) {
			const ConstPool *cpool = bin->constant_pool[i];
			if (!cpool) {
				continue;
			}
			tag = java_constant_pool_tag_name(cpool);
			if (!tag) {
				rz_warn_if_reached();
				continue;
			}
			snprintf(number, sizeof(number), "#%u", i);
			text = java_constant_pool_stringify(cpool);
			rz_strbuf_appendf(sb, "  %*s = %-19s %s\n", padding, number, tag, text);
			free(text);
		}
	}
}

RZ_API void rz_bin_java_class_const_pool_as_json(RzBinJavaClass *bin, PJ *j) {
	rz_return_if_fail(bin && j);
	const char *tag;
	char *text;
	pj_a(j);
	if (bin->constant_pool) {
		for (ut32 i = 0; i < bin->constant_pool_count; ++i) {
			const ConstPool *cpool = bin->constant_pool[i];
			if (!cpool) {
				continue;
			}
			tag = java_constant_pool_tag_name(cpool);
			if (!tag) {
				rz_warn_if_reached();
				continue;
			}
			text = java_constant_pool_stringify(cpool);
			pj_o(j);
			pj_kn(j, "index", i);
			pj_kn(j, "tag_n", cpool->tag);
			pj_ks(j, "tag_s", tag);
			pj_ks(j, "value", text ? text : "");
			pj_end(j);
			free(text);
		}
	}
	pj_end(j);
}

static RzBinSection *new_section(const char *name, ut64 start, ut64 end, ut32 perm) {
	RzBinSection *section = RZ_NEW0(RzBinSection);
	if (!section) {
		rz_warn_if_reached();
		return NULL;
	}
	section->name = strdup(name);
	if (!section->name) {
		rz_warn_if_reached();
		free(section);
		return NULL;
	}
	section->paddr = start;
	section->vaddr = start;
	section->size = end - start;
	section->vsize = section->size;
	section->perm = perm;
	section->add = true;
	return section;
}

static void section_free(void *u) {
	rz_bin_section_free((RzBinSection *)u);
}

RZ_API RzList *rz_bin_java_class_as_sections(RzBinJavaClass *bin) {
	rz_return_val_if_fail(bin, NULL);

	RzList *sections = rz_list_newf(section_free);
	if (!sections) {
		return NULL;
	}
	char *tmp;
	char secname[256];
	ut64 end_offset;
	if (bin->constant_pool) {
		rz_list_append(sections,
			new_section("class.constant_pool",
				bin->constant_pool_offset,
				bin->interfaces_offset,
				RZ_PERM_R));
	}
	if (bin->interfaces) {
		rz_list_append(sections,
			new_section("class.interfaces",
				bin->interfaces_offset,
				bin->fields_offset,
				RZ_PERM_R));
	}
	if (bin->fields) {
		rz_list_append(sections,
			new_section("class.fields",
				bin->fields_offset,
				bin->methods_offset,
				RZ_PERM_R));
		for (ut32 i = 0; i < bin->fields_count; ++i) {
			Field *field = bin->fields[i];
			if (!field) {
				continue;
			}
			tmp = java_class_constant_pool_stringify_at(bin, field->name_index);
			if (!tmp) {
				rz_warn_if_reached();
				continue;
			}
			snprintf(secname, sizeof(secname), "class.fields.%s.attr", tmp);
			free(tmp);
			if ((i + 1) < bin->fields_count && bin->fields[i + 1]) {
				end_offset = bin->fields[i + 1]->offset;
			} else {
				end_offset = bin->methods_offset;
			}
			rz_list_append(sections, new_section(secname, field->offset, end_offset, RZ_PERM_R));
		}
	}
	if (bin->methods) {
		rz_list_append(sections,
			new_section("class.methods",
				bin->methods_offset,
				bin->attributes_offset,
				RZ_PERM_R));
		for (ut32 i = 0; i < bin->methods_count; ++i) {
			Method *method = bin->methods[i];
			if (!method || method->attributes_count < 1) {
				continue;
			}
			tmp = java_class_constant_pool_stringify_at(bin, method->name_index);
			if (!tmp) {
				rz_warn_if_reached();
				continue;
			}
			snprintf(secname, sizeof(secname), "class.methods.%s.attr", tmp);
			if ((i + 1) < bin->methods_count && bin->methods[i + 1]) {
				end_offset = bin->methods[i + 1]->offset;
			} else {
				end_offset = bin->attributes_offset;
			}
			rz_list_append(sections, new_section(secname, method->offset, end_offset, RZ_PERM_R));

			if (!method->attributes) {
				free(tmp);
				continue;
			}
			for (ut32 k = 0; k < method->attributes_count; ++k) {
				Attribute *attr = method->attributes[k];
				if (attr && attr->type == ATTRIBUTE_TYPE_CODE) {
					AttributeCode *ac = (AttributeCode *)attr->info;
					snprintf(secname, sizeof(secname), "class.methods.%s.attr.%d.code", tmp, k);
					ut64 size = ac->code_offset + attr->attribute_length;
					rz_list_append(sections, new_section(secname, ac->code_offset, size, RZ_PERM_R | RZ_PERM_X));
					break;
				}
			}
			free(tmp);
		}
	}
	if (bin->attributes) {
		rz_list_append(sections,
			new_section("class.attr",
				bin->attributes_offset,
				bin->class_end_offset,
				RZ_PERM_R));
	}

	return sections;
}

static int compare_strings(const void *a, const void *b) {
	return strcmp((const char *)a, (const char *)b);
}

RZ_API RzList *rz_bin_java_class_as_libraries(RzBinJavaClass *bin) {
	rz_return_val_if_fail(bin, NULL);

	RzList *list = rz_list_newf(free);
	if (!list) {
		return NULL;
	}
	ut16 arg0, arg1;
	char *tmp;

	if (bin->constant_pool) {
		for (ut32 i = 0; i < bin->constant_pool_count; ++i) {
			tmp = NULL;
			const ConstPool *cpool = bin->constant_pool[i];
			if (!cpool) {
				continue;
			}
			if (cpool->tag == CONSTANT_POOL_CLASS) {
				if (java_constant_pool_resolve(cpool, &arg0, &arg1) != 1) {
					rz_warn_if_reached();
					continue;
				}
				// arg0 is name_index
				tmp = java_class_constant_pool_stringify_at(bin, arg0);
			} else if (java_constant_pool_is_import(cpool)) {
				if (java_constant_pool_resolve(cpool, &arg0, &arg1) != 2) {
					rz_warn_if_reached();
					continue;
				}
				// arg0 is name_and_type_index
				const ConstPool *nat = java_class_constant_pool_at(bin, arg0);
				if (!nat ||
					java_constant_pool_resolve(nat, &arg0, &arg1) != 1) {
					rz_warn_if_reached();
					continue;
				}
				// arg0 is name_index
				tmp = java_class_constant_pool_stringify_at(bin, arg0);
			}
			if (tmp && !rz_list_find(list, tmp, compare_strings)) {
				rz_list_append(list, tmp);
			}
		}
	}
	return list;
}

RZ_API void rz_bin_java_class_interfaces_as_text(RzBinJavaClass *bin, RzStrBuf *sb) {
	rz_return_if_fail(bin && sb);

	ut16 index;
	char number[16];
	char *tmp = NULL;
	rz_strbuf_appendf(sb, "Interfaces: %u\n", bin->interfaces_count);
	if (bin->interfaces) {
		int padding = calculate_padding_ut16(bin->constant_pool_count) + 1;
		for (ut32 i = 0; i < bin->interfaces_count; ++i) {
			if (!bin->interfaces[i]) {
				continue;
			}
			const ConstPool *cpool = java_class_constant_pool_at(bin, bin->interfaces[i]->index);
			if (!cpool || java_constant_pool_resolve(cpool, &index, NULL) != 1) {
				rz_warn_if_reached();
				continue;
			}
			snprintf(number, sizeof(number), "#%u", i);
			tmp = java_class_constant_pool_stringify_at(bin, index);
			rz_str_replace_char(tmp, '/', '.');
			rz_strbuf_appendf(sb, "  %*s = #%-5u %s\n", padding, number, index, tmp);
			free(tmp);
		}
	}
}

RZ_API void rz_bin_java_class_interfaces_as_json(RzBinJavaClass *bin, PJ *j) {
	rz_return_if_fail(bin && j);
	pj_a(j);
	char *tmp = NULL;
	ut16 index;
	if (bin->interfaces) {
		for (ut32 i = 0; i < bin->interfaces_count; ++i) {
			if (!bin->interfaces[i]) {
				continue;
			}

			const ConstPool *cpool = java_class_constant_pool_at(bin, bin->interfaces[i]->index);
			if (!cpool || java_constant_pool_resolve(cpool, &index, NULL) != 1) {
				rz_warn_if_reached();
				continue;
			}
			pj_o(j);
			pj_kn(j, "offset", bin->interfaces[i]->offset);
			pj_kn(j, "name_n", bin->interfaces[i]->index);
			tmp = java_class_constant_pool_stringify_at(bin, index);
			rz_str_replace_char(tmp, '/', '.');
			pj_ks(j, "name_s", tmp ? tmp : "");
			free(tmp);
			pj_end(j);
		}
	}
	pj_end(j);
}
