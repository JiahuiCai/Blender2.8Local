/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2014 Blender Foundation.
 * All rights reserved.
 *
 * Contributor(s): Blender Foundation
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file cage2d_manipulator.c
 *  \ingroup wm
 *
 * \name Cage Manipulator
 *
 * 2D Manipulator
 *
 * \brief Rectangular manipulator acting as a 'cage' around its content.
 * Interacting scales or translates the manipulator.
 */

#include "BIF_gl.h"

#include "BKE_context.h"

#include "BLI_math.h"
#include "BLI_rect.h"

#include "ED_screen.h"
#include "ED_view3d.h"
#include "ED_manipulator_library.h"

#include "GPU_matrix.h"
#include "GPU_shader.h"
#include "GPU_immediate.h"
#include "GPU_select.h"

#include "MEM_guardedalloc.h"

#include "RNA_access.h"
#include "RNA_define.h"

#include "WM_api.h"
#include "WM_types.h"

/* own includes */
#include "../manipulator_library_intern.h"

/* wmManipulator->highlight_part */
enum {
	ED_MANIPULATOR_RECT_TRANSFORM_INTERSECT_TRANSLATE     = 1,
	ED_MANIPULATOR_RECT_TRANSFORM_INTERSECT_SCALEX_LEFT   = 2,
	ED_MANIPULATOR_RECT_TRANSFORM_INTERSECT_SCALEX_RIGHT  = 3,
	ED_MANIPULATOR_RECT_TRANSFORM_INTERSECT_SCALEY_UP     = 4,
	ED_MANIPULATOR_RECT_TRANSFORM_INTERSECT_SCALEY_DOWN   = 5,
};

#define MANIPULATOR_RECT_MIN_WIDTH 15.0f
#define MANIPULATOR_RESIZER_WIDTH  20.0f

/* -------------------------------------------------------------------- */

static void rect_transform_draw_corners(
        const rctf *r, const float offsetx, const float offsety, const float color[3])
{
	uint pos = GWN_vertformat_attr_add(immVertexFormat(), "pos", GWN_COMP_F32, 2, GWN_FETCH_FLOAT);

	immBindBuiltinProgram(GPU_SHADER_2D_UNIFORM_COLOR);
	immUniformColor3fv(color);

	immBegin(GWN_PRIM_LINES, 16);

	immVertex2f(pos, r->xmin, r->ymin + offsety);
	immVertex2f(pos, r->xmin, r->ymin);
	immVertex2f(pos, r->xmin, r->ymin);
	immVertex2f(pos, r->xmin + offsetx, r->ymin);

	immVertex2f(pos, r->xmax, r->ymin + offsety);
	immVertex2f(pos, r->xmax, r->ymin);
	immVertex2f(pos, r->xmax, r->ymin);
	immVertex2f(pos, r->xmax - offsetx, r->ymin);

	immVertex2f(pos, r->xmax, r->ymax - offsety);
	immVertex2f(pos, r->xmax, r->ymax);
	immVertex2f(pos, r->xmax, r->ymax);
	immVertex2f(pos, r->xmax - offsetx, r->ymax);

	immVertex2f(pos, r->xmin, r->ymax - offsety);
	immVertex2f(pos, r->xmin, r->ymax);
	immVertex2f(pos, r->xmin, r->ymax);
	immVertex2f(pos, r->xmin + offsetx, r->ymax);

	immEnd();

	immUnbindProgram();
}

static void rect_transform_draw_interaction(
        const float col[4], const int highlighted,
        const float half_w, const float half_h,
        const float w, const float h, const float line_width)
{
	/* Why generate coordinates for 4 vertices, when we only use three? */
	float verts[4][2];

	switch (highlighted) {
		case ED_MANIPULATOR_RECT_TRANSFORM_INTERSECT_SCALEX_LEFT:
			verts[0][0] = -half_w + w;
			verts[0][1] = -half_h;
			verts[1][0] = -half_w;
			verts[1][1] = -half_h;
			verts[2][0] = -half_w;
			verts[2][1] = half_h;
			verts[3][0] = -half_w + w;
			verts[3][1] = half_h;
			break;

		case ED_MANIPULATOR_RECT_TRANSFORM_INTERSECT_SCALEX_RIGHT:
			verts[0][0] = half_w - w;
			verts[0][1] = -half_h;
			verts[1][0] = half_w;
			verts[1][1] = -half_h;
			verts[2][0] = half_w;
			verts[2][1] = half_h;
			verts[3][0] = half_w - w;
			verts[3][1] = half_h;
			break;

		case ED_MANIPULATOR_RECT_TRANSFORM_INTERSECT_SCALEY_DOWN:
			verts[0][0] = -half_w;
			verts[0][1] = -half_h + h;
			verts[1][0] = -half_w;
			verts[1][1] = -half_h;
			verts[2][0] = half_w;
			verts[2][1] = -half_h;
			verts[3][0] = half_w;
			verts[3][1] = -half_h + h;
			break;

		case ED_MANIPULATOR_RECT_TRANSFORM_INTERSECT_SCALEY_UP:
			verts[0][0] = -half_w;
			verts[0][1] = half_h - h;
			verts[1][0] = -half_w;
			verts[1][1] = half_h;
			verts[2][0] = half_w;
			verts[2][1] = half_h;
			verts[3][0] = half_w;
			verts[3][1] = half_h - h;
			break;

		default:
			return;
	}

	Gwn_VertFormat *format = immVertexFormat();
	uint pos = GWN_vertformat_attr_add(format, "pos", GWN_COMP_F32, 2, GWN_FETCH_FLOAT);
	uint color = GWN_vertformat_attr_add(format, "color", GWN_COMP_F32, 3, GWN_FETCH_FLOAT);
	immBindBuiltinProgram(GPU_SHADER_2D_FLAT_COLOR);

	glLineWidth(line_width + 3.0f);

	immBegin(GWN_PRIM_LINE_STRIP, 3);
	immAttrib3f(color, 0.0f, 0.0f, 0.0f);
	immVertex2fv(pos, verts[0]);
	immVertex2fv(pos, verts[1]);
	immVertex2fv(pos, verts[2]);
	immEnd();

	glLineWidth(line_width);

	immBegin(GWN_PRIM_LINE_STRIP, 3);
	immAttrib3fv(color, col);
	immVertex2fv(pos, verts[0]);
	immVertex2fv(pos, verts[1]);
	immVertex2fv(pos, verts[2]);
	immEnd();

	immUnbindProgram();
}

static void manipulator_rect_transform_draw_intern(
        wmManipulator *mpr, const bool select, const bool highlight, const int select_id)
{
	const bool use_clamp = (mpr->parent_mgroup->type->flag & WM_MANIPULATORGROUPTYPE_3D) == 0;
	float dims[2];
	RNA_float_get_array(mpr->ptr, "dimensions", dims);
	float w = dims[0];
	float h = dims[1];

	const int transform_flag = RNA_enum_get(mpr->ptr, "transform");

	float aspx = 1.0f, aspy = 1.0f;
	const float half_w = w / 2.0f;
	const float half_h = h / 2.0f;
	const rctf r = {
		.xmin = -half_w,
		.ymin = -half_h,
		.xmax = half_w,
		.ymax = half_h,
	};

	gpuPushMatrix();
	gpuMultMatrix(mpr->matrix_space);
	gpuMultMatrix(mpr->matrix_basis);
	gpuMultMatrix(mpr->matrix_offset);

	if (w > h) {
		aspx = h / w;
	}
	else {
		aspy = w / h;
	}

	if (use_clamp) {
		w = min_ff(aspx * w / MANIPULATOR_RESIZER_WIDTH, MANIPULATOR_RESIZER_WIDTH);
		h = min_ff(aspy * h / MANIPULATOR_RESIZER_WIDTH, MANIPULATOR_RESIZER_WIDTH);
	}
	else {
		/* Corner size. */
		w = h = min_ff(w * aspx, h * aspy) / 10.0f;
	}

	/* corner manipulators */
	glLineWidth(mpr->line_width + 3.0f);

	rect_transform_draw_corners(&r, w, h, (const float[3]){0, 0, 0});

	/* corner manipulators */
	{
		float col[4];
		manipulator_color_get(mpr, highlight, col);
		glLineWidth(mpr->line_width);
		rect_transform_draw_corners(&r, w, h, col);
	}

	if (select) {
		if (transform_flag & ED_MANIPULATOR_RECT_TRANSFORM_FLAG_SCALE) {
			int scale_parts[] = {
			    ED_MANIPULATOR_RECT_TRANSFORM_INTERSECT_SCALEX_LEFT,
				ED_MANIPULATOR_RECT_TRANSFORM_INTERSECT_SCALEX_RIGHT,
				ED_MANIPULATOR_RECT_TRANSFORM_INTERSECT_SCALEY_UP,
				ED_MANIPULATOR_RECT_TRANSFORM_INTERSECT_SCALEY_DOWN,
			};
			for (int i = 0; i < ARRAY_SIZE(scale_parts); i++) {
				GPU_select_load_id(select_id | scale_parts[i]);
				rect_transform_draw_interaction(
				        mpr->color, scale_parts[i], half_w, half_h,
				        w, h, mpr->line_width);
			}
		}
	}
	else {
		rect_transform_draw_interaction(
		        mpr->color, mpr->highlight_part, half_w, half_h,
		        w, h, mpr->line_width);
	}

	glLineWidth(1.0);
	gpuPopMatrix();
}

/**
 * For when we want to draw 2d cage in 3d views.
 */
static void manipulator_rect_transform_draw_select(const bContext *UNUSED(C), wmManipulator *mpr, int select_id)
{
	manipulator_rect_transform_draw_intern(mpr, true, false, select_id);
}

static void manipulator_rect_transform_draw(const bContext *UNUSED(C), wmManipulator *mpr)
{
	const bool is_highlight = (mpr->state & WM_MANIPULATOR_STATE_HIGHLIGHT) != 0;
	manipulator_rect_transform_draw_intern(mpr, false, is_highlight, -1);
}

static int manipulator_rect_transform_get_cursor(wmManipulator *mpr)
{
	int highlight_part = mpr->highlight_part;

	if (mpr->parent_mgroup->type->flag & WM_MANIPULATORGROUPTYPE_3D) {
		return BC_NSEW_SCROLLCURSOR;
	}

	switch (highlight_part) {
		case ED_MANIPULATOR_RECT_TRANSFORM_INTERSECT_TRANSLATE:
			return BC_HANDCURSOR;
		case ED_MANIPULATOR_RECT_TRANSFORM_INTERSECT_SCALEX_LEFT:
		case ED_MANIPULATOR_RECT_TRANSFORM_INTERSECT_SCALEX_RIGHT:
			return CURSOR_X_MOVE;
		case ED_MANIPULATOR_RECT_TRANSFORM_INTERSECT_SCALEY_DOWN:
		case ED_MANIPULATOR_RECT_TRANSFORM_INTERSECT_SCALEY_UP:
			return CURSOR_Y_MOVE;
		default:
			return CURSOR_STD;
	}
}

static int manipulator_rect_transform_test_select(
        bContext *C, wmManipulator *mpr, const wmEvent *event)
{
	//float matrot[2][2];
	float point_local[2];
	float dims[2];
	RNA_float_get_array(mpr->ptr, "dimensions", dims);
	float w = dims[0];
	float h = dims[1];
	float half_w = w / 2.0f;
	float half_h = h / 2.0f;
	float aspx = 1.0f, aspy = 1.0f;

	if (manipulator_window_project_2d(
	        C, mpr, (const float[2]){UNPACK2(event->mval)}, 2, true, point_local) == false)
	{
		return 0;
	}

	const int transform_flag = RNA_enum_get(mpr->ptr, "transform");
	if (dims[0] > dims[1]) {
		aspx = h / w;
	}
	else {
		aspy = w / h;
	}
	w = min_ff(aspx * w / MANIPULATOR_RESIZER_WIDTH, MANIPULATOR_RESIZER_WIDTH);
	h = min_ff(aspy * h / MANIPULATOR_RESIZER_WIDTH, MANIPULATOR_RESIZER_WIDTH);


	rctf r;

	r.xmin = -half_w + w;
	r.ymin = -half_h + h;
	r.xmax = half_w - w;
	r.ymax = half_h - h;

	bool isect = BLI_rctf_isect_pt_v(&r, point_local);

	if (isect)
		return ED_MANIPULATOR_RECT_TRANSFORM_INTERSECT_TRANSLATE;

	/* if manipulator does not have a scale intersection, don't do it */
	if (transform_flag &
	    (ED_MANIPULATOR_RECT_TRANSFORM_FLAG_SCALE | ED_MANIPULATOR_RECT_TRANSFORM_FLAG_SCALE_UNIFORM))
	{
		r.xmin = -half_w;
		r.ymin = -half_h;
		r.xmax = -half_w + w;
		r.ymax = half_h;

		isect = BLI_rctf_isect_pt_v(&r, point_local);

		if (isect)
			return ED_MANIPULATOR_RECT_TRANSFORM_INTERSECT_SCALEX_LEFT;

		r.xmin = half_w - w;
		r.ymin = -half_h;
		r.xmax = half_w;
		r.ymax = half_h;

		isect = BLI_rctf_isect_pt_v(&r, point_local);

		if (isect)
			return ED_MANIPULATOR_RECT_TRANSFORM_INTERSECT_SCALEX_RIGHT;

		r.xmin = -half_w;
		r.ymin = -half_h;
		r.xmax = half_w;
		r.ymax = -half_h + h;

		isect = BLI_rctf_isect_pt_v(&r, point_local);

		if (isect)
			return ED_MANIPULATOR_RECT_TRANSFORM_INTERSECT_SCALEY_DOWN;

		r.xmin = -half_w;
		r.ymin = half_h - h;
		r.xmax = half_w;
		r.ymax = half_h;

		isect = BLI_rctf_isect_pt_v(&r, point_local);

		if (isect)
			return ED_MANIPULATOR_RECT_TRANSFORM_INTERSECT_SCALEY_UP;
	}

	return 0;
}

typedef struct RectTransformInteraction {
	float orig_mouse[2];
	float orig_matrix_offset[4][4];
} RectTransformInteraction;

static bool manipulator_rect_transform_get_prop_value(
        wmManipulator *mpr, wmManipulatorProperty *mpr_prop, float *value)
{
	if (STREQ(mpr_prop->type->idname, "offset")) {
		WM_manipulator_target_property_value_get_array(mpr, mpr_prop, value);
	}
	else if (STREQ(mpr_prop->type->idname, "scale")) {
		const int transform_flag = RNA_enum_get(mpr->ptr, "transform");
		if (transform_flag & ED_MANIPULATOR_RECT_TRANSFORM_FLAG_SCALE_UNIFORM) {
			if (WM_manipulator_target_property_array_length(mpr, mpr_prop) == 2) {
				WM_manipulator_target_property_value_get_array(mpr, mpr_prop, value);
			}
			else {
				*value = WM_manipulator_target_property_value_get(mpr, mpr_prop);
				value[1] = value[0];
			}
		}
		else {
			WM_manipulator_target_property_value_get_array(mpr, mpr_prop, value);
		}
	}
	else {
		BLI_assert(0);
	}

	return true;
}

static void manipulator_rect_transform_setup(wmManipulator *mpr)
{
	mpr->flag |= WM_MANIPULATOR_DRAW_MODAL;
}

static void manipulator_rect_transform_invoke(
        bContext *C, wmManipulator *mpr, const wmEvent *event)
{
	RectTransformInteraction *data = MEM_callocN(sizeof(RectTransformInteraction), "cage_interaction");

	copy_m4_m4(data->orig_matrix_offset, mpr->matrix_offset);

	if (manipulator_window_project_2d(
	        C, mpr, (const float[2]){UNPACK2(event->mval)}, 2, false, data->orig_mouse) == 0)
	{
		zero_v2(data->orig_mouse);
	}

	mpr->interaction_data = data;
}

static void manipulator_rect_transform_modal(
        bContext *C, wmManipulator *mpr, const wmEvent *event,
        eWM_ManipulatorTweak UNUSED(tweak_flag))
{
	const int transform_flag = RNA_enum_get(mpr->ptr, "transform");
	const bool pivot_center = (transform_flag & ED_MANIPULATOR_RECT_TRANSFORM_FLAG_TRANSLATE) == 0;
	RectTransformInteraction *data = mpr->interaction_data;
#if 0
	/* needed here as well in case clamping occurs */
	const bool use_clamp = (mpr->parent_mgroup->type->flag & WM_MANIPULATORGROUPTYPE_3D) == 0;
	const float orig_ofx = mpr->matrix_offset[3][0];
	const float orig_ofy = mpr->matrix_offset[3][1];
#endif

	float point_local[2];

	if (manipulator_window_project_2d(
	        C, mpr, (const float[2]){UNPACK2(event->mval)}, 2, false, point_local) == false)
	{
		return;
	}

	float value_x = (point_local[0] - data->orig_mouse[0]);
	float value_y = (point_local[1] - data->orig_mouse[1]);

	float dims[2];
	RNA_float_get_array(mpr->ptr, "dimensions", dims);

	const float  orig_scale[2] = {data->orig_matrix_offset[0][0], data->orig_matrix_offset[1][1]};
	const float *orig_offset = data->orig_matrix_offset[3];

	float  scale[2] = {mpr->matrix_offset[0][0], mpr->matrix_offset[1][1]};
	float *offset = mpr->matrix_offset[3];

	if (mpr->highlight_part == ED_MANIPULATOR_RECT_TRANSFORM_INTERSECT_TRANSLATE) {
		offset[0] = orig_offset[0] + value_x;
		offset[1] = orig_offset[1] + value_y;
	}
	else if (mpr->highlight_part == ED_MANIPULATOR_RECT_TRANSFORM_INTERSECT_SCALEX_LEFT) {
		value_x = min_ff(value_x, (dims[0] * orig_scale[0]) * (pivot_center ? 2 : 1));
		if (pivot_center == false) {
			offset[0] = orig_offset[0] + value_x / 2.0f;
		}
		scale[0] = (dims[0] * orig_scale[0] - value_x) / dims[0];
	}
	else if (mpr->highlight_part == ED_MANIPULATOR_RECT_TRANSFORM_INTERSECT_SCALEX_RIGHT) {
		value_x = max_ff(value_x, (dims[0] * orig_scale[0]) * (pivot_center ? -2 : -1));
		if (pivot_center == false) {
			offset[0] = orig_offset[0] + value_x / 2.0f;
		}
		scale[0] = (dims[0] * orig_scale[0] + value_x) / dims[0];
	}
	else if (mpr->highlight_part == ED_MANIPULATOR_RECT_TRANSFORM_INTERSECT_SCALEY_DOWN) {
		int a = (transform_flag & ED_MANIPULATOR_RECT_TRANSFORM_FLAG_SCALE_UNIFORM) ? 0 : 1;
		value_y = min_ff(value_y, (dims[1] * orig_scale[a]) * (pivot_center ? 2 : 1));
		if (pivot_center == false) {
			offset[1] = orig_offset[1] + value_y / 2.0f;
		}
		scale[a] = (dims[1] * orig_scale[a] - value_y) / dims[1];
	}
	else if (mpr->highlight_part == ED_MANIPULATOR_RECT_TRANSFORM_INTERSECT_SCALEY_UP) {
		int a = (transform_flag & ED_MANIPULATOR_RECT_TRANSFORM_FLAG_SCALE_UNIFORM) ? 0 : 1;
		value_y = max_ff(value_y, (dims[1] * orig_scale[a]) * (pivot_center ? -2 : -1));
		if (pivot_center == false) {
			offset[1] = orig_offset[1] + value_y / 2.0f;
		}
		scale[a] = (dims[1] * orig_scale[a] + value_y) / dims[1];
	}
	else {
		BLI_assert(0);
	}

	/* TODO(campbell): Complicates things too much since not all scales are in the same space. */
#if 0
	/* clamping - make sure manipulator is at least 5 pixels wide */
	if (use_clamp == false) {
		/* pass */
	}
	else if (transform_flag & ED_MANIPULATOR_RECT_TRANSFORM_FLAG_SCALE_UNIFORM) {
		if (scale[0] < MANIPULATOR_RECT_MIN_WIDTH / max_ff(dims[0], dims[1])) {
			scale[0] = max_ff(MANIPULATOR_RECT_MIN_WIDTH / dims[1], MANIPULATOR_RECT_MIN_WIDTH / dims[0]);
			offset[0] = orig_ofx;
			offset[1] = orig_ofy;
		}
	}
	else {
		if (scale[0] < MANIPULATOR_RECT_MIN_WIDTH / dims[0]) {
			scale[0] = MANIPULATOR_RECT_MIN_WIDTH / dims[0];
			offset[0] = orig_ofx;
		}
		if (scale[1] < MANIPULATOR_RECT_MIN_WIDTH / dims[1]) {
			scale[1] = MANIPULATOR_RECT_MIN_WIDTH / dims[1];
			offset[1] = orig_ofy;
		}
	}
#endif

	{
		wmManipulatorProperty *mpr_prop = WM_manipulator_target_property_find(mpr, "scale");
		if (mpr_prop->type != NULL) {
			float range[2];
			if (WM_manipulator_target_property_range_get(mpr, mpr_prop, range)) {
				CLAMP(scale[0], range[0], range[1]);
				CLAMP(scale[1], range[0], range[1]);
			}
		}
	}

	/* Needed for when we're uniform transforming a 2D vector and need to write both. */
	if (transform_flag & ED_MANIPULATOR_RECT_TRANSFORM_FLAG_SCALE_UNIFORM) {
		scale[1] = scale[0];
	}

	mpr->matrix_offset[0][0] = scale[0];
	mpr->matrix_offset[1][1] = scale[1];

	wmManipulatorProperty *mpr_prop;

	mpr_prop = WM_manipulator_target_property_find(mpr, "offset");
	if (mpr_prop->type != NULL) {
		WM_manipulator_target_property_value_set_array(C, mpr, mpr_prop, offset);
	}

	mpr_prop = WM_manipulator_target_property_find(mpr, "scale");
	if (mpr_prop->type != NULL) {
		if (transform_flag & ED_MANIPULATOR_RECT_TRANSFORM_FLAG_SCALE_UNIFORM) {
			scale[1] = scale[0];
			if (WM_manipulator_target_property_array_length(mpr, mpr_prop) == 2) {
				WM_manipulator_target_property_value_set_array(C, mpr, mpr_prop, scale);
			}
			else {
				WM_manipulator_target_property_value_set(C, mpr, mpr_prop, scale[0]);
			}
		}
		else {
			WM_manipulator_target_property_value_set_array(C, mpr, mpr_prop, scale);
		}
	}

	/* tag the region for redraw */
	ED_region_tag_redraw(CTX_wm_region(C));
}

static void manipulator_rect_transform_property_update(wmManipulator *mpr, wmManipulatorProperty *mpr_prop)
{
	if (STREQ(mpr_prop->type->idname, "offset")) {
		manipulator_rect_transform_get_prop_value(mpr, mpr_prop, mpr->matrix_offset[3]);
	}
	else if (STREQ(mpr_prop->type->idname, "scale")) {
		float scale[2];
		manipulator_rect_transform_get_prop_value(mpr, mpr_prop, scale);
		mpr->matrix_offset[0][0] = scale[0];
		mpr->matrix_offset[1][1] = scale[1];
	}
	else {
		BLI_assert(0);
	}
}

static void manipulator_rect_transform_exit(bContext *C, wmManipulator *mpr, const bool cancel)
{
	RectTransformInteraction *data = mpr->interaction_data;

	if (!cancel)
		return;

	wmManipulatorProperty *mpr_prop;

	/* reset properties */
	mpr_prop = WM_manipulator_target_property_find(mpr, "offset");
	if (mpr_prop->type != NULL) {
		WM_manipulator_target_property_value_set_array(C, mpr, mpr_prop, data->orig_matrix_offset[3]);
	}

	mpr_prop = WM_manipulator_target_property_find(mpr, "scale");
	if (mpr_prop->type != NULL) {
		const float orig_scale[2] = {data->orig_matrix_offset[0][0], data->orig_matrix_offset[1][1]};
		const int transform_flag = RNA_enum_get(mpr->ptr, "transform");
		if (transform_flag & ED_MANIPULATOR_RECT_TRANSFORM_FLAG_SCALE_UNIFORM) {
			if (WM_manipulator_target_property_array_length(mpr, mpr_prop) == 2) {
				WM_manipulator_target_property_value_set_array(C, mpr, mpr_prop, orig_scale);
			}
			else {
				WM_manipulator_target_property_value_set(C, mpr, mpr_prop, orig_scale[0]);
			}
		}
		else {
			WM_manipulator_target_property_value_set_array(C, mpr, mpr_prop, orig_scale);
		}
	}

	copy_m4_m4(mpr->matrix_offset, data->orig_matrix_offset);
}


/* -------------------------------------------------------------------- */
/** \name Cage Manipulator API
 *
 * \{ */

static void MANIPULATOR_WT_cage_2d(wmManipulatorType *wt)
{
	/* identifiers */
	wt->idname = "MANIPULATOR_WT_cage_2d";

	/* api callbacks */
	wt->draw = manipulator_rect_transform_draw;
	wt->draw_select = manipulator_rect_transform_draw_select;
	wt->setup = manipulator_rect_transform_setup;
	wt->invoke = manipulator_rect_transform_invoke;
	wt->property_update = manipulator_rect_transform_property_update;
	wt->modal = manipulator_rect_transform_modal;
	wt->test_select = manipulator_rect_transform_test_select;
	wt->exit = manipulator_rect_transform_exit;
	wt->cursor_get = manipulator_rect_transform_get_cursor;

	wt->struct_size = sizeof(wmManipulator);

	/* rna */
	static EnumPropertyItem rna_enum_transform[] = {
		{ED_MANIPULATOR_RECT_TRANSFORM_FLAG_TRANSLATE, "TRANSLATE", 0, "Translate", ""},
		{ED_MANIPULATOR_RECT_TRANSFORM_FLAG_ROTATE, "ROTATE", 0, "Rotate", ""},
		{ED_MANIPULATOR_RECT_TRANSFORM_FLAG_SCALE, "SCALE", 0, "Scale", ""},
		{ED_MANIPULATOR_RECT_TRANSFORM_FLAG_SCALE_UNIFORM, "SCALE_UNIFORM", 0, "Scale Uniform", ""},
		{0, NULL, 0, NULL, NULL}
	};
	static float unit_v2[2] = {1.0f, 1.0f};
	RNA_def_float_vector(wt->srna, "dimensions", 2, unit_v2, 0, FLT_MAX, "Dimensions", "", 0.0f, FLT_MAX);
	RNA_def_enum_flag(wt->srna, "transform", rna_enum_transform, 0, "Transform Options", "");

	WM_manipulatortype_target_property_def(wt, "offset", PROP_FLOAT, 2);
	WM_manipulatortype_target_property_def(wt, "scale", PROP_FLOAT, 2);
}

void ED_manipulatortypes_cage_2d(void)
{
	WM_manipulatortype_append(MANIPULATOR_WT_cage_2d);
}

/** \} */
