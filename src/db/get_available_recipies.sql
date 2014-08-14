-- Determine which cocktails can be made based on the ingredients
-- currently configured in the dispenser table

.mode column
.width 6 32 128

SELECT 		r.id, r.name, r.description, sum(not vegan), sum(alcoholic)
FROM 		recipe r,
			recipe_ingredient ri,
			ingredient i
WHERE 		r.id = ri.recipe_id
AND			i.id = ri.ingredient_id
AND			r.id NOT IN (
	-- sub-select lists IDs of recipes which have missing ingredients
	SELECT 		r.id
	FROM 		recipe_ingredient ri, 
			recipe r, 
			ingredient i 
	WHERE 		r.id = ri.recipe_id
	AND 		ri.ingredient_id = i.id
	AND 		NOT EXISTS (
		SELECT 	1
		FROM	dispenser d
		WHERE	d.ingredient_id = i.id
	)
)
GROUP BY	r.id, r.name, r.description ;

