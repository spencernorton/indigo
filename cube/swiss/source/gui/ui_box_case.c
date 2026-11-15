#include <math.h>
#include <stddef.h>

#include "ui_box_case.h"

#define BOX_CAMERA 700.0f
#define BOX_HALF_WIDTH 90.0f
#define BOX_HALF_HEIGHT 120.0f
#define BOX_DEPTH 18.0f
#define BOX_DEGREES 0.01745329252f

/* Cubic Hermite paths pass through the settled poses without a velocity
 * corner. The depth's first tangent is zero so the path is smooth through
 * the centre too. Yaw accounts for the camera's off-axis view: the first
 * neighbour turns 65 degrees in world space, about 50 towards the viewer.
 * The outer one turns farther, leaving room for all five cases on stage. */
static float path(float distance, const float values[4],
	const float tangents[4])
{
	int segment = distance < 1.0f ? 0 : distance < 2.0f ? 1 : 2;
	float t = distance - (float)segment;
	float t2 = t * t;
	float t3 = t2 * t;

	return (2.0f * t3 - 3.0f * t2 + 1.0f) * values[segment] +
		(t3 - 2.0f * t2 + t) * tangents[segment] +
		(-2.0f * t3 + 3.0f * t2) * values[segment + 1] +
		(t3 - t2) * tangents[segment + 1];
}

static uiBoxCasePoint_t project(float x, float y, float z, float centre,
	float depth, float sine, float cosine)
{
	float rotatedX = cosine * x + sine * z;
	float rotatedZ = -sine * x + cosine * z;
	float scale = BOX_CAMERA / (BOX_CAMERA + depth + rotatedZ);
	uiBoxCasePoint_t point = {
		320.0f + (centre + rotatedX) * scale,
		216.0f + y * scale
	};

	return point;
}

static float area(const uiBoxCaseQuad_t *quad)
{
	float twiceArea = 0.0f;
	int i;

	for(i = 0; i < 4; ++i) {
		const uiBoxCasePoint_t *a = &quad->point[i];
		const uiBoxCasePoint_t *b = &quad->point[(i + 1) % 4];
		twiceArea += a->x * b->y - b->x * a->y;
	}
	return twiceArea;
}

bool UIBoxCase_Pose(float slot, uiBoxCasePose_t *pose)
{
	static const float centres[4] = {0.0f, 190.0f, 363.0f, 500.0f};
	static const float centreTangents[4] = {200.0f, 181.5f, 155.0f, 120.0f};
	static const float depths[4] = {0.0f, 110.0f, 240.0f, 350.0f};
	static const float depthTangents[4] = {0.0f, 120.0f, 120.0f, 100.0f};
	static const float yaws[4] = {0.0f, 65.0f, 85.0f, 87.0f};
	static const float yawTangents[4] = {75.0f, 32.0f, 3.0f, 0.0f};
	static const float corners[4][2] = {
		{-BOX_HALF_WIDTH, -BOX_HALF_HEIGHT},
		{BOX_HALF_WIDTH, -BOX_HALF_HEIGHT},
		{BOX_HALF_WIDTH, BOX_HALF_HEIGHT},
		{-BOX_HALF_WIDTH, BOX_HALF_HEIGHT}
	};
	uiBoxCasePoint_t back[4];
	uiBoxCasePose_t result;
	float distance;
	float sign;
	float centre;
	float depth;
	float yaw;
	float sine;
	float cosine;
	int i;

	if(pose == NULL || !isfinite(slot) || fabsf(slot) > 3.0f) {
		return false;
	}
	distance = fabsf(slot);
	sign = slot < 0.0f ? -1.0f : 1.0f;
	centre = sign * path(distance, centres, centreTangents);
	depth = path(distance, depths, depthTangents);
	yaw = sign * path(distance, yaws, yawTangents) * BOX_DEGREES;
	sine = sinf(yaw);
	cosine = cosf(yaw);

	for(i = 0; i < 4; ++i) {
		result.front.point[i] = project(corners[i][0], corners[i][1],
			0.0f, centre, depth, sine, cosine);
		back[i] = project(corners[i][0], corners[i][1], BOX_DEPTH,
			centre, depth, sine, cosine);
	}
	if(slot < 0.0f) {
		result.side = (uiBoxCaseQuad_t) {{back[0], result.front.point[0],
			result.front.point[3], back[3]}};
	}
	else {
		result.side = (uiBoxCaseQuad_t) {{result.front.point[1], back[1],
			back[2], result.front.point[2]}};
	}
	/* Near the centre the camera cannot see either side. Collapsing the
	 * hidden face also prevents its back-facing projected area from leaking
	 * out through the translucent front while art is loading. */
	if(area(&result.side) <= 0.0f) {
		int edge = slot < 0.0f ? 0 : 1;
		result.side = (uiBoxCaseQuad_t) {{result.front.point[edge],
			result.front.point[edge], result.front.point[3 - edge],
			result.front.point[3 - edge]}};
	}
	*pose = result;
	return true;
}

static uiBoxCasePoint_t mixPoint(uiBoxCasePoint_t a, uiBoxCasePoint_t b,
	float weight)
{
	uiBoxCasePoint_t point = {
		(1.0f - weight) * a.x + weight * b.x,
		(1.0f - weight) * a.y + weight * b.y
	};
	return point;
}

static bool frontHeights(const uiBoxCaseQuad_t *front, float *left,
	float *right)
{
	float height;
	int i;

	if(front == NULL) return false;
	for(i = 0; i < 4; ++i) {
		if(!isfinite(front->point[i].x) || !isfinite(front->point[i].y)) {
			return false;
		}
	}
	*left = fabsf(front->point[3].y - front->point[0].y);
	*right = fabsf(front->point[2].y - front->point[1].y);
	height = fmaxf(*left, *right);
	if(*left > 0.0f && *right > 0.0f && isfinite(height)) {
		/* Height is inverse camera depth. Normalize first so even very
		 * large finite coordinates cannot overflow the weighted sum. */
		*left /= height;
		*right /= height;
	}
	else {
		*left = *right = 1.0f;
	}
	return true;
}

static float frontWeight(float u, float leftHeight, float rightHeight)
{
	float denominator = (1.0f - u) * rightHeight + u * leftHeight;
	return denominator > 0.0f ? u * leftHeight / denominator : u;
}

bool UIBoxCase_Strips(const uiBoxCaseQuad_t *front, uiBoxCaseStrips_t *out)
{
	uiBoxCaseStrips_t result;
	uiBoxCasePoint_t edge[2][2];
	float left;
	float right;
	int column;
	int end;

	if(out == NULL || !frontHeights(front, &left, &right)) return false;
	for(end = 0; end < 2; ++end) {
		float v = end ? 0.966667f : 0.033333f;
		edge[0][end] = mixPoint(front->point[0], front->point[3], v);
		edge[1][end] = mixPoint(front->point[1], front->point[2], v);
	}
	for(column = 0; column <= UI_BOX_CASE_STRIPS; ++column) {
		float u = 0.033333f + 0.933334f *
			(float)column / (float)UI_BOX_CASE_STRIPS;
		float weight = frontWeight(u, left, right);
		for(end = 0; end < 2; ++end) {
			uiBoxCasePoint_t point = mixPoint(edge[0][end], edge[1][end], weight);
			if(!isfinite(point.x) || !isfinite(point.y)) return false;
			result.column[column][end] = point;
		}
	}
	*out = result;
	return true;
}
