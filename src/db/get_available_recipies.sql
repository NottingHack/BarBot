-- Determine which cocktails can be made based on the ingredients
-- currently configured in the dispenser table

SELECT 		r.id, r.name, r.description, gt.name, SUM(NOT(i.vegan)) > 0, SUM(i.alcoholic) > 0
FROM 		recipe r,
			recipe_ingredient ri,
			ingredient i,
			glass_type gt 
WHERE 		r.id NOT IN (
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
AND 		ri.ingredient_id = i.id
AND 		ri.recipe_id = r.id
AND			r.glass_type_id = gt.id
GROUP BY    r.name, r.name, r.description;


