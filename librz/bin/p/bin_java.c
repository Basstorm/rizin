// SPDX-FileCopyrightText: 2009-2019 pancake <pancake@nopcode.org>
// SPDX-FileCopyrightText: 2009-2019 nibble <nibble.ds@gmail.com>
// SPDX-FileCopyrightText: 2009-2019 Adam Pridgen <dso@rice.edu>
// SPDX-License-Identifier: LGPL-3.0-only

#include <rz_types.h>
#include <rz_util.h>
#include <rz_lib.h>
#include <rz_bin.h>

#include "../format/java/new/class_bin.h"

#define rz_bin_file_get_java_class(bf) ((RzBinJavaClass *)bf->o->bin_obj)

static RzBinInfo *info(RzBinFile *bf) {
	RzBinJavaClass *jclass = rz_bin_file_get_java_class(bf);
	if (!jclass) {
		return NULL;
	}
	RzBinInfo *binfo = RZ_NEW0(RzBinInfo);
	if (!binfo) {
		return NULL;
	}
	binfo->lang /*      */ = rz_bin_java_class_language(jclass);
	binfo->file /*      */ = strdup(bf->file);
	binfo->type /*      */ = strdup("JAVA CLASS");
	binfo->bclass /*    */ = rz_bin_java_class_version(jclass);
	binfo->has_va /*    */ = false;
	binfo->rclass /*    */ = strdup("class");
	binfo->os /*        */ = strdup("any");
	binfo->subsystem /* */ = strdup("any");
	binfo->machine /*   */ = strdup("jvm");
	binfo->arch /*      */ = strdup("java");
	binfo->bits /*      */ = 32;
	binfo->big_endian /**/ = true;
	binfo->dbg_info /*  */ = rz_bin_java_class_debug_info(jclass);
	return binfo;
}

static bool load_buffer(RzBinFile *bf, void **bin_obj, RzBuffer *buf, ut64 loadaddr, Sdb *sdb) {
	RzBinJavaClass *jclass = rz_bin_java_class_new(buf, loadaddr, sdb);
	if (!jclass) {
		return false;
	}
	*bin_obj = jclass;
	return true;
}

static void destroy(RzBinFile *bf) {
	rz_bin_java_class_free(rz_bin_file_get_java_class(bf));
}

static bool check_buffer(RzBuffer *b) {
	if (rz_buf_size(b) > 32) {
		ut8 buf[4];
		rz_buf_read_at(b, 0, buf, sizeof(buf));
		return !memcmp(buf, "\xca\xfe\xba\xbe", 4);
	}
	return false;
}

static ut64 baddr(RzBinFile *bf) {
	return 0;
}

static Sdb *get_sdb(RzBinFile *bf) {
	return bf->sdb;
}

static void free_rz_bin_class(void /*RzBinClass*/ *k) {
	RzBinClass *bclass = k;
	if (bclass) {
		rz_list_free(bclass->methods);
		rz_list_free(bclass->fields);
		free(bclass->name);
		free(bclass->super);
		free(bclass->visibility_str);
		free(bclass);
	}
}

static RzList *classes(RzBinFile *bf) {
	RzBinClass *bclass = NULL;
	RzList *classes = NULL;
	RzBinJavaClass *jclass = rz_bin_file_get_java_class(bf);
	if (!jclass) {
		return NULL;
	}

	classes = rz_list_newf(free_rz_bin_class);
	if (!classes) {
		return NULL;
	}

	bclass = RZ_NEW0(RzBinClass);
	if (!bclass) {
		rz_list_free(classes);
		return NULL;
	}
	rz_list_append(classes, bclass);

	bclass->name = rz_bin_java_class_name(jclass);
	bclass->super = rz_bin_java_class_super(jclass);
	bclass->visibility = rz_bin_java_class_access_flags(jclass);
	bclass->visibility_str = rz_bin_java_class_access_flags_readable(jclass);

	bclass->methods = rz_bin_java_class_methods_as_symbols(jclass);
	bclass->fields = rz_bin_java_class_fields_as_binfields(jclass);
	if (!bclass->methods || !bclass->fields) {
		rz_list_free(classes);
		return NULL;
	}

	return classes;
}

static RzList *imports(RzBinFile *bf) {
	RzBinJavaClass *jclass = rz_bin_file_get_java_class(bf);
	if (!jclass) {
		return NULL;
	}

	return rz_bin_java_class_const_pool_as_imports(jclass);
}

static RzList *sections(RzBinFile *bf) {
	RzBinJavaClass *jclass = rz_bin_file_get_java_class(bf);
	if (!jclass) {
		return NULL;
	}

	return rz_bin_java_class_as_sections(jclass);
}

static RzList *symbols(RzBinFile *bf) {
	RzList *tmp;
	RzBinJavaClass *jclass = rz_bin_file_get_java_class(bf);
	if (!jclass) {
		return NULL;
	}

	RzList *list = rz_bin_java_class_methods_as_symbols(jclass);
	if (!list) {
		return NULL;
	}

	tmp = rz_bin_java_class_fields_as_symbols(jclass); 
	if(!rz_list_join(list, tmp)) {
		rz_list_free(tmp);
	}

	tmp = rz_bin_java_class_const_pool_as_symbols(jclass);
	if(!rz_list_join(list, tmp)) {
		rz_list_free(tmp);
	}
	return list;
}

static RzList *fields(RzBinFile *bf) {
	RzBinJavaClass *jclass = rz_bin_file_get_java_class(bf);
	if (!jclass) {
		return NULL;
	}

	return rz_bin_java_class_fields_as_binfields(jclass);
}

static RzList *libs(RzBinFile *bf) {
	RzBinJavaClass *jclass = rz_bin_file_get_java_class(bf);
	if (!jclass) {
		return NULL;
	}

	return rz_bin_java_class_as_libraries(jclass);
}

static RzBinAddr *binsym(RzBinFile *bf, int sym) {
	RzBinJavaClass *jclass = rz_bin_file_get_java_class(bf);
	if (!jclass) {
		return NULL;
	}

	return rz_bin_java_class_resolve_symbol(jclass, sym);
}

static RzList *entrypoints(RzBinFile *bf) {
	RzBinJavaClass *jclass = rz_bin_file_get_java_class(bf);
	if (!jclass) {
		return NULL;
	}

	return rz_bin_java_class_entrypoints(jclass);
}

static RzList *strings(RzBinFile *bf) {
	RzBinJavaClass *jclass = rz_bin_file_get_java_class(bf);
	if (!jclass) {
		return NULL;
	}

	return rz_bin_java_class_strings(jclass);
}

static int demangle_type(const char *str) {
	return RZ_BIN_NM_JAVA;
}

RzBinPlugin rz_bin_plugin_java = {
	.name = "java",
	.desc = "java bin plugin",
	.license = "LGPL3",
	.get_sdb = &get_sdb,
	.load_buffer = &load_buffer,
	.destroy = &destroy,
	.check_buffer = &check_buffer,
	.baddr = &baddr,
	.binsym = &binsym,
	.entries = &entrypoints,
	.sections = sections,
	.symbols = symbols,
	.imports = &imports,
	.strings = &strings,
	.info = &info,
	.fields = fields,
	.libs = libs,
	.classes = classes,
	.demangle_type = demangle_type,
	.minstrlen = 3,
};

#ifndef RZ_PLUGIN_INCORE
RZ_API RzLibStruct rizin_plugin = {
	.type = RZ_LIB_TYPE_BIN,
	.data = &rz_bin_plugin_java,
	.version = RZ_VERSION
};
#endif
