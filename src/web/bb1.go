
package main

import (
  "fmt"
  "regexp"
  "os"
  "html/template"
  "net/http"
  "database/sql"
  _ "github.com/mattn/go-sqlite3"
  "time"
  "strings"
  "github.com/tarm/goserial"
  "strconv"
  "flag"
  "bufio"
  auth "github.com/abbot/go-http-auth"
)

const ORDER_FMT = "%05d"


type Recipe struct {
  Id   int
  Name string
  Description string
  Selected bool
  GlassName string
  ImageName string
  Vegan bool
  Alcoholic bool
  VeganIcon string
  AlcoholIcon string
}

type DrinksMenu struct {
  Title     string
  Recipes   []Recipe
  Admin     bool
}

type MenuItemIngredient struct {
  Id      int
  Name    string
  ActQty  int
  UoM     string
  Manual  bool
}

type MenuItem struct {
  Id          int
  DrinkName   string
  Description string
  Direct      bool
  Dietary     string
  Ingredients []MenuItemIngredient
}

type OrderLogged struct {
 // Id int
  OrderId string
}

type OrderSent struct {
  OrderId     string
  Success     bool
  FailReason  string
  Ingredients []MenuItemIngredient
}

type OrderDetails struct {
  DrinkName   string
  Alcohol     bool
  Vegan       bool
  IdCheck     bool
  OrderRef    string
  OrderRefs   []string  // list of order refs for order selection list on left of screen
  Ingredients []MenuItemIngredient
  Glass       GlassType
}

type RecipeDetails struct {
  DrinkName   string
  Alcohol     bool
  Vegan       bool
  Ingredients []MenuItemIngredient
  Glass       GlassType
  RecipeID    string
}
type DispenserIngredients struct {
  Id int
  Name string
  Current bool // Currently selected
}

type DispenserDetails struct {
  Id  int
  Name string
  Ingredients []DispenserIngredients
}

type AdminRecipeIngr struct {
  Id    int
  Name  string
  Qty   int
  UoM   string
}

type GlassType struct {
  Id              int
  Name            string
  Selected        bool
}

type AdminRecipe struct {
  RecipieName     string
  RecipieId       int
  RecipieSelected bool
  Recipes         []Recipe
  GlassTypes      []GlassType
  AllIngredients  []AdminRecipeIngr  // All known ingrediants for "Add" listbox
  RecIngredients  []AdminRecipeIngr  // Ingrediants in currently selected receipe
}

type AdminDispenser struct {
  Id              int
  Name            string
  Rail_position   int
  Ingredient      string
}

type AdminControl struct {
  Dispensers      []AdminDispenser
}

type AdminHeader struct {
  AllowMaint      bool
}

const (
  DISPENSER_OPTIC    = 1
  DISPENSER_MIXER    = 2
  DISPENSER_DASHER   = 3
  DISPENSER_SYRINGE  = 4
  DISPENSER_CONVEYOR = 5
  DISPENSER_STIRRER  = 6
  DISPENSER_SLICE    = 7
  DISPENSER_UMBRELLA = 8
)


var BarbotSerialChan chan []string
var Direct bool
var Password string
var AllowMaint bool

// showMenu displays the list of available drinks to the user
func showMenu(db *sql.DB, w http.ResponseWriter) {
  tmpl, _ := template.ParseFiles("main_header.html", "menu.html", "main_footer.html")
  menu := DrinksMenu{"Drinks", getReceipes(db), false}

  tmpl.ExecuteTemplate(w, "main_header", nil)
  tmpl.ExecuteTemplate(w, "menu"       , menu)
  tmpl.ExecuteTemplate(w, "main_footer", nil)
}

func getReceipes(db *sql.DB) ([]Recipe) {
  var recipes []Recipe

      // Load drinks - only show those that can currently be made
  rows, err := db.Query(`   SELECT          r.id, r.name, r.description, gt.name, NOT(SUM(NOT(i.vegan)) > 0), SUM(i.alcoholic) > 0
							FROM            recipe r,
											recipe_ingredient ri,
											ingredient i,
											glass_type gt
							WHERE           r.id NOT IN (
									-- sub-select lists IDs of recipes which have missing ingredients
									SELECT          r.id
									FROM            recipe_ingredient ri,
													recipe r,
													ingredient i
									WHERE           r.id = ri.recipe_id
									AND             ri.ingredient_id = i.id
									AND             NOT EXISTS (
											SELECT  1
											FROM    dispenser d
											WHERE   d.ingredient_id = i.id
									)
							)
							AND             ri.ingredient_id = i.id
							AND             ri.recipe_id = r.id
							AND             r.glass_type_id = gt.id
							GROUP BY        r.name, r.name, r.description, gt.name`)
      if err != nil {
        panic(fmt.Sprintf("%v", err))
      }
      defer rows.Close()

      for rows.Next() {
        var recipe Recipe
    rows.Scan(&recipe.Id, &recipe.Name, &recipe.Description, &recipe.GlassName, &recipe.Vegan, &recipe.Alcoholic)
	recipe.ImageName = getDrinkIcon(recipe.Name, recipe.GlassName)

	if recipe.Vegan {
		recipe.VeganIcon = "/static/images/vegan.png"
	} else {
		recipe.VeganIcon = "/static/images/animal-product.png"
	}

	if recipe.Alcoholic {
		recipe.AlcoholIcon = "/static/images/alcohol.png"
	} else {
		recipe.AlcoholIcon = "/static/images/non-alcoholic.png"
	}
        recipes = append(recipes, recipe)
      }
      rows.Close()

  return recipes
}

// showMenuItem shows details of a drink selected from the menu (ingredients, etc)
func showMenuItem(db *sql.DB, w http.ResponseWriter, r *http.Request) {
      var menuitem MenuItem
      drink_id := r.URL.Path[len("/menu/"):]

      // Get basic receipe information
	row := db.QueryRow("select id, name, description from recipe where id = ?", drink_id)
	err := row.Scan(&menuitem.Id, &menuitem.DrinkName, &menuitem.Description)
      if err == sql.ErrNoRows {
        http.NotFound(w, r)
        return
      }

	menuitem.Dietary = ""
    tx, _ := db.Begin()
    defer tx.Rollback()
    if recipeContainsAlcohol(tx, strconv.Itoa(menuitem.Id)) {
		menuitem.Dietary += "One or more ingredients in this recipe contain alcohol. "
	} else {
		menuitem.Dietary += "There is no alcohol in this recipe. "
    }

    if recipeIsVegan(tx, strconv.Itoa(menuitem.Id)) {
		menuitem.Dietary += "This recipe is vegan. "
	} else {
		menuitem.Dietary += "One or more ingredients in this recipe contain animal products. "
    }

	menuitem.Direct = Direct
      menuitem.Ingredients = getRecipeIngrediants(db, drink_id)

      t, _ := template.ParseFiles("menu_item.html")
      t.Execute(w, menuitem)
}

func getRecipeIngrediants(db *sql.DB, drink_id string) ([]MenuItemIngredient) {
  var ingrediants []MenuItemIngredient

  // Get details of the ingredients
  sql := `
    select
      i.id,
      i.name,
      ri.qty * dt.unit_size as act_act,
      case when ri.qty = 1 then dt.unit_name else dt.unit_plural end as uom,
      dt.manual
    from recipe r
    inner join recipe_ingredient ri on ri.recipe_id = r.id
    inner join ingredient i on i.id = ri.ingredient_id
    inner join dispenser_type dt on dt.id = i.dispenser_type_id
    where r.id = ?`

  rows, err := db.Query(sql, drink_id)
  if err != nil {
    panic(fmt.Sprintf("%v", err))
  }
  defer rows.Close()

  for rows.Next() {
    var ingr MenuItemIngredient
    rows.Scan(&ingr.Id, &ingr.Name, &ingr.ActQty, &ingr.UoM, &ingr.Manual)
    ingrediants = append(ingrediants, ingr)
  }

  return ingrediants
}

// drinksMenuHandler handles request to "/menu/[n]" - either showing all the drinks available, or details on the selected drink
func drinksMenuHandler(w http.ResponseWriter, r *http.Request) {

    // Open database
    db := getDBConnection()
    defer db.Close()

    if len(r.URL.Path) <= len("/menu/") {
      showMenu(db, w)
    } else {
      showMenuItem(db, w, r)
    }
}

func adminHandler(w http.ResponseWriter, r *http.Request) {

  // Default admin page is dispenser config for now. So if no subpage is specified, redirect to that
  if (r.URL.Path == "/admin") || (r.URL.Path == "/admin/") {
    http.Redirect(w, r, "/admin/dispenser/", http.StatusSeeOther)
    return
  }

  req_page := r.URL.Path[len("/admin/"):]

  switch {
    case strings.HasPrefix(req_page, "dispenser/"):
      adminDispenser(w, r, req_page[len("dispenser/"):])
      return;

    case strings.HasPrefix(req_page, "recipe/"):
      adminRecipe(w, r, req_page[len("recipe/"):])
      return;

    case strings.HasPrefix(req_page, "control/"):
      adminControl(w, r, req_page[len("control/"):])
      return;

    case strings.HasPrefix(req_page, "menu/"):
      adminMenu(w, r, req_page[len("menu/"):])
      return;

    case strings.HasPrefix(req_page, "maintenance/"):
      adminMaintenance(w, r, req_page[len("maintenance/"):])
      return;

    default:
      http.NotFound(w, r)
      return
  }
}


// adminRecipe allows a recipe to be added / amended
func adminRecipe(w http.ResponseWriter, r *http.Request, param string) {

  tmpl, _ := template.ParseFiles("admin_header.html", "admin_recipe.html", "admin_footer.html")

  // Open database
  db := getDBConnection()
  defer db.Close()
  r.ParseForm()

  recipe_id, err := strconv.Atoi(r.Form.Get("recipe_selection"))
  if err != nil {
    recipe_id = -1
  }

  if (param == "add_drink") {
    // returned form is receipe_name=<drink name entered>
    if len(r.Form.Get("recipe_add")) <= 1 {
      http.Redirect(w, r, "/admin/recipe/", http.StatusSeeOther)
      return
    }

    // Get glass selection
    glass_type_id, err := strconv.Atoi(r.Form.Get("glass_selection"))
    if err != nil {
      http.Redirect(w, r, "/admin/recipe/", http.StatusSeeOther)
      return
    }

    _, err = db.Exec("insert into recipe (name, glass_type_id) values (?, ?)", r.Form.Get("recipe_add"), glass_type_id)
    if err != nil {
      panic(fmt.Sprintf("Failed to update db: %v", err))
    }

    // get inserted id
    row := db.QueryRow("select max(id) from recipe")
    err = row.Scan(&recipe_id)
    if err != nil {
      http.Redirect(w, r, "/admin/recipe/", http.StatusSeeOther)
      return
    }

    // http.Redirect(w, r, "/admin/recipe/", http.StatusSeeOther)
    // return
  }

  if (param == "add_ingrediant") {
    // returned form is wanting to add an ingrediant to a drink
// NSERT INTO recipe_ingredient (recipe_id, ingredient_id, seq, qty) SELECT r.id, i.id, 4, 1 FROM recipe r, ingredient i WHERE r.name = 'Gin and tonic (lemon lime)' AND i.name = 'Lemon';

    ingredient_id, err := strconv.Atoi(r.Form.Get("ingrediant_selection"))
    if err != nil {
      http.Redirect(w, r, "/admin/recipe/", http.StatusSeeOther)
    }

    ingredient_id_remove, err := strconv.Atoi(r.Form.Get("remove_ingr"))
    if err != nil {
      ingredient_id_remove = -1
    }
    ingredient_qty, err := strconv.Atoi(r.Form.Get("ingrediant_qty"))
    if err != nil {
      ingredient_qty = -1
    }

    // Default to a quantity of 1 if nothing entered or invalid entry
    if ingredient_qty <= 0 {
      ingredient_qty = 1
    }

    if ingredient_id_remove > 0 {
      _, err := db.Exec("delete from recipe_ingredient where recipe_id=? and ingredient_id=? ", recipe_id, ingredient_id_remove)
      if err != nil {
        panic(fmt.Sprintf("Failed to update db: %v", err))
      }
    } else {

      // get next seq number
      var seq_num int
      row := db.QueryRow("select max(seq)+1 from recipe_ingredient where recipe_id=?", recipe_id)
      err = row.Scan(&seq_num)
      if err != nil {
        seq_num = 1
      }


      _, err = db.Exec("insert into recipe_ingredient (recipe_id, ingredient_id, seq, qty) values (?, ?, ?, ?)", recipe_id, ingredient_id, seq_num, ingredient_qty)
      if err != nil {
        panic(fmt.Sprintf("Failed to update db (add ingrediant): %v", err))
      }
    }
    //  http.Redirect(w, r, "/admin/recipe/", http.StatusSeeOther)
  //   return
  }

  var adminR AdminRecipe


  if (recipe_id > 0) {
    adminR.RecipieSelected = true
  } else
  {
    adminR.RecipieSelected = false
  }

  // Get a list of all drinks for list box
  rows, err := db.Query("select r.id, r.name, r.glass_type_id from recipe r order by r.name")
  if err != nil {
    panic(fmt.Sprintf("%v", err))
  }
  defer rows.Close()
  glass_type_id := -1
  var tmp_glass_type_id int
  for rows.Next() {
    var recipe Recipe
    rows.Scan(&recipe.Id, &recipe.Name, &tmp_glass_type_id)
    if recipe_id == recipe.Id {
      recipe.Selected = true
      glass_type_id = tmp_glass_type_id
    } else {
      recipe.Selected = false
    }
    adminR.Recipes = append(adminR.Recipes, recipe)
  }
  rows.Close()

  // Get a list of glass types for the glass selection listbox
  rows, err = db.Query("select g.id, g.name from glass_type g order by g.name")
  if err != nil {
    panic(fmt.Sprintf("%v", err))
  }
  defer rows.Close()

  for rows.Next() {
    var glass GlassType
    rows.Scan(&glass.Id, &glass.Name)
    if glass.Id == glass_type_id {
      glass.Selected = true
    } else {
      glass.Selected = false
    }
    adminR.GlassTypes = append(adminR.GlassTypes, glass)
  }
  rows.Close()

  // Get a list of all ingrediants for the "add" list box
  rows, err = db.Query("select i.id, i.name from ingredient i order by i.name")
  if err != nil {
    panic(fmt.Sprintf("%v", err))
  }
  defer rows.Close()

  for rows.Next() {
    var recipeIngr AdminRecipeIngr
    rows.Scan(&recipeIngr.Id, &recipeIngr.Name)
    adminR.AllIngredients = append(adminR.AllIngredients, recipeIngr)
  }
  rows.Close()

  // Get a list of all ingrediants in the currently selected drink
  adminR.RecipieId = recipe_id

  sqlstr :=
    `   select
          i.id,
          i.name,
          ri.qty * dt.unit_size,
          case when ri.qty = 1 then dt.unit_name else dt.unit_plural end as uom
        from recipe_ingredient ri
        inner join ingredient i on ri.ingredient_id = i.id
        inner join dispenser_type dt on dt.id = i.dispenser_type_id
        where ri.recipe_id = ?
        order by ri.seq`

  rows, err = db.Query(sqlstr, recipe_id)
  if err != nil {
    panic(fmt.Sprintf("%v", err))
  }
  defer rows.Close()

  for rows.Next() {
    var recipeIngr AdminRecipeIngr
    rows.Scan(&recipeIngr.Id, &recipeIngr.Name, &recipeIngr.Qty, &recipeIngr.UoM)
    adminR.RecIngredients = append(adminR.RecIngredients, recipeIngr)
  }
  rows.Close()

  var adminHead AdminHeader
  adminHead.AllowMaint = AllowMaint

  tmpl.ExecuteTemplate(w, "admin_header", adminHead)
  tmpl.ExecuteTemplate(w, "admin_recipe", adminR)
  tmpl.ExecuteTemplate(w, "admin_footer", nil)
  return
}


// adminDispenser shows the despenser selection page of the admin interface
func adminDispenser(w http.ResponseWriter, r *http.Request, param string) {

  tmpl, _ := template.ParseFiles("admin_header.html", "admin_dispenser.html", "admin_footer.html")

  // Open database
  db := getDBConnection()
  defer db.Close()

  if (param == "update") {
    // returned form is dispenser_id=ingredient_id
    r.ParseForm()

    for dispenser_id, ingredient_id := range r.Form {
      _, err := db.Exec(
              "update dispenser set ingredient_id = ? where id = ?",
              ingredient_id[0],
              dispenser_id,
      )
      if err != nil {
        panic(fmt.Sprintf("Failed to update db: %v", err))
      }

    }

    http.Redirect(w, r, "/admin/dispenser/", http.StatusSeeOther)
    return
  }

  var dispensers = make([]DispenserDetails,21) // TODO: Do not hard code number of dispenersers...

  for i:=0; i < 21; i++ {
    var ingr DispenserIngredients
    ingr.Id = -1
    ingr.Name = "(None)"
    dispensers[i].Ingredients = append(dispensers[i].Ingredients, ingr)
  }
  // Get a list of all dispensers, possible ingrediants and current ingrediant
  sql := `
    select
      d.id as dispenser_id,
      d.name as dispenser_name,
      case when d.ingredient_id = i.id then 1 else 0 end as current,
      i.id as ingredient_id,
      i.name as ingredient_name,
      ifnull(d.ingredient_id, -1)
    from dispenser d
    inner join dispenser_type dt on dt.id = d.dispenser_type_id
    left outer join ingredient i on d.dispenser_type_id = i.dispenser_type_id
    where dt.manual = 0
    order by d.id, i.name
  `

  rows, err := db.Query(sql)
  if err != nil {
    // TODO
    panic(fmt.Sprintf("%v", err))
  }
  defer rows.Close()

  for rows.Next() {
    var ingr DispenserIngredients
    var dispenser_id int
    var dispenser_name string
    var current int
    var ingredient_id int

    rows.Scan(&dispenser_id, &dispenser_name, &current, &ingr.Id, &ingr.Name, ingredient_id)
    if current==1 {
      ingr.Current = true
    } else {
      ingr.Current = false
    }
    if ingredient_id==-1 {
      dispensers[dispenser_id].Ingredients[0].Current = true // set the "none" ingrediant to current
    }
    dispensers[dispenser_id].Ingredients = append(dispensers[dispenser_id].Ingredients, ingr)
    dispensers[dispenser_id].Name = dispenser_name
    dispensers[dispenser_id].Id = dispenser_id
  }

  var adminHead AdminHeader
  adminHead.AllowMaint = AllowMaint
  tmpl.ExecuteTemplate(w, "admin_header", adminHead)
  tmpl.ExecuteTemplate(w, "admin_dispenser", dispensers)
  tmpl.ExecuteTemplate(w, "admin_footer", nil)
  return
}

func adminControl(w http.ResponseWriter, r *http.Request, param string) {
  tmpl, _ := template.ParseFiles("admin_header.html", "admin_control.html", "admin_footer.html")
  var cmd string

  // Open database
  db := getDBConnection()
  defer db.Close()

  sendmsg := true

  // Get reguested command from param (e.g. for move/1234, set cmd = move)
  i := strings.Index(param, "/")
  if i > 0 {
    cmd = param[:i]
  } else {
    cmd = param
  }
  cmdlist := make([]string, 1)

  switch (cmd) {
    case "reset":
      cmdlist[0] = "R"

    case "zero":
      cmdlist[0] = "C"               // Clear current instructions
      cmdlist = append(cmdlist, "Z") // Zero
      cmdlist = append(cmdlist, "G") // Go

    case "move":
      rail_position, err := strconv.Atoi(param[len("move/"):])
      if err != nil {
        return
      }
      cmdlist[0] = "C"               // Clear current instructions
      cmdlist = append(cmdlist, fmt.Sprintf("M %d", rail_position)) // Move to rail position nnnn
      cmdlist = append(cmdlist, "G") // Go

    case "dispense":
      var dispenser_id int
      var dispenser_param int

      ret, err := fmt.Sscanf(param[len("dispense/"):], "%d/%d", &dispenser_id, &dispenser_param)
      if err != nil || ret != 2 {
        return
      }
      cmdlist[0] = "C"
      cmdlist = append(cmdlist, fmt.Sprintf("D %d %d", dispenser_id, dispenser_param))
      cmdlist = append(cmdlist, "G")

    case "md": // Move then dispense
      var dispenser_id int
      var dispenser_param int
      var rail_position int

      ret, err := fmt.Sscanf(param[len("md/"):], "%d/%d/%d", &rail_position, &dispenser_id, &dispenser_param)
      if err != nil || ret != 3 {
        return
      }
      cmdlist[0] = "C"
      cmdlist = append(cmdlist, fmt.Sprintf("M %d", rail_position)) // Move to rail position nnnn
      cmdlist = append(cmdlist, fmt.Sprintf("D %d %d", dispenser_id, dispenser_param)) // Dispense from <dispenser_id> with param <dispenser_param>
      cmdlist = append(cmdlist, "G")

    default:
      sendmsg = false
  }

  if (sendmsg) {
    BarbotSerialChan <- cmdlist
    return
  }

  // Get a list of all dispensers
  sql := `
    select
      d.id as dispenser_id,
      d.name as dispenser_name,
      d.rail_position,
      ifnull(i.name, '(None)')
    from dispenser d
    inner join dispenser_type dt on dt.id = d.dispenser_type_id
    left outer join ingredient i on i.id = d.ingredient_id
    where dt.manual = 0
    order by d.id
  `

  rows, err := db.Query(sql)
  if err != nil {
    panic(fmt.Sprintf("%v", err))
  }
  defer rows.Close()

  var control AdminControl
  for rows.Next() {
    var dispenser AdminDispenser

    rows.Scan(&dispenser.Id, &dispenser.Name, &dispenser.Rail_position, &dispenser.Ingredient)
    control.Dispensers = append(control.Dispensers, dispenser)
  }

  var adminHead AdminHeader
  adminHead.AllowMaint = AllowMaint
  tmpl.ExecuteTemplate(w, "admin_header" , adminHead)
  tmpl.ExecuteTemplate(w, "admin_control", control)
  tmpl.ExecuteTemplate(w, "admin_footer" , nil)
  return
}

func adminMaintenance(w http.ResponseWriter, r *http.Request, param string) {
  tmpl, _ := template.ParseFiles("admin_header.html", "admin_maintenance.html", "admin_footer.html")

  if AllowMaint == false {
    return;
  }
  cmd := make([]string, 1)

  switch (param) {
    case "enter":
      cmd[0] = "A 0 1"

    case "leave":
      cmd[0] = "A 0 0"

    case "opticIdle":
      cmd[0] = "A 1 0"

    case "opticDispense":
      cmd[0] = "A 1 9"

    case "mixerIdle":
      cmd[0] = "A 2 0"

    case "mixerDispense":
      cmd[0] = "A 2 9"

    case "d0-on":
      cmd[0] = "A 3 0"

    case "d1-on":
      cmd[0] = "A 3 1"

    case "d2-on":
      cmd[0] = "A 3 2"

    case "d0-off":
      cmd[0] = "A 4 0"

    case "d1-off":
      cmd[0] = "A 4 1"

    case "d2-off":
      cmd[0] = "A 4 2"

    default:
      var adminHead AdminHeader
      adminHead.AllowMaint = AllowMaint
      tmpl.ExecuteTemplate(w, "admin_header" , adminHead)
      tmpl.ExecuteTemplate(w, "admin_maintenance", nil)
      tmpl.ExecuteTemplate(w, "admin_footer" , nil)
      return
  }

  BarbotSerialChan <- cmd

}


func adminMenu(w http.ResponseWriter, r *http.Request, param string) {
  tmpl, _ := template.ParseFiles("admin_header.html", "menu.html", "admin_recipe_details.html", "admin_footer.html")

  // Open database
  db := getDBConnection()
  defer db.Close()

  if strings.HasPrefix(param, "details/") {
    recipe_id := param[len("details/"):]

    // Assume recipe_id passed in (->404 if not), so also get the details of that drink
    sqlstr := `
      select
        r.name,
        gt.id,
        gt.name
      from recipe r
      inner join glass_type gt on r.glass_type_id = gt.id
      where r.id = ?`
    var receipe RecipeDetails
    row := db.QueryRow(sqlstr, recipe_id)

    err := row.Scan(&receipe.DrinkName, &receipe.Glass.Id, &receipe.Glass.Name)
    if err == sql.ErrNoRows {
      http.NotFound(w, r)
      return
    } else {
      if err != nil {
        panic(fmt.Sprintf("adminMenu - failed to get recipe details: %#v", err))
      }
    }

    tx, _ := db.Begin()
    defer tx.Rollback()
    receipe.Alcohol = recipeContainsAlcohol(tx, recipe_id)

    // Get list of ingrediants
    receipe.Ingredients = getRecipeIngrediants(db, recipe_id)
    receipe.RecipeID = recipe_id

    var adminHead AdminHeader
    adminHead.AllowMaint = AllowMaint
    tmpl.ExecuteTemplate(w, "admin_header", adminHead)
    tmpl.ExecuteTemplate(w, "admin_recipe_details", receipe)
    tmpl.ExecuteTemplate(w, "admin_footer", nil)
    return
  }

  if strings.HasPrefix(param, "make/") {
    if !makeOrderDirect(db, w, r, param[len("make/"):]) {
      http.NotFound(w, r)
      return
    }
    return
  }

  menu := DrinksMenu{"Drinks", getReceipes(db), true}

  var adminHead AdminHeader
  adminHead.AllowMaint = AllowMaint
  tmpl.ExecuteTemplate(w, "admin_header", adminHead)
  tmpl.ExecuteTemplate(w, "menu"        , menu)
  tmpl.ExecuteTemplate(w, "admin_footer", nil)
  return
}

// orderListHandler handles requests to /orderlist/
func orderListHandler(w http.ResponseWriter, r *http.Request) {

    // Open database
    db := getDBConnection()
    defer db.Close()


    sqlstr := "select id from drink_order where cancelled = 0 and made_end_ts is null"

    rows, err := db.Query(sqlstr)
    if err != nil {
      // TODO
      panic(fmt.Sprintf("%v", err))
    }
    defer rows.Close()

    var orderdetails OrderDetails
    for rows.Next() {
      var id int
      rows.Scan(&id)
      orderdetails.OrderRefs = append(orderdetails.OrderRefs, fmt.Sprintf(ORDER_FMT, id))
    }

    if len(r.URL.Path) > len("/orderlist/") {

      // Check if user has clicked on a on order
      var p string = r.URL.Path[len("/orderlist/"):]
      switch  {
        case strings.HasPrefix(p, "remove/"):
          removeOrder(db, w, r, p[len("remove/"):])
          http.Redirect(w, r, "/orderlist", http.StatusSeeOther)
          return

        case strings.HasPrefix(p, "make/"):
          if !makeOrder(db, w, r, p[len("make/"):]) {
            http.NotFound(w, r)
          }
          return

        case strings.HasPrefix(p, "complete/"):
          if !completeOrder(db, w, r, p[len("complete/"):]) {
            http.NotFound(w, r)
          }
          return
      }

      // Assume order ref passed in (->404 if not), so also get the details of that order
      orderdetails.OrderRef = r.URL.Path[len("/orderlist/"):]

      sqlstr = `
        select
          do.alcohol,
          do.id_checked,
          r.name,
          do.recipe_id,
          gt.id,
          gt.name
        from drink_order do
        inner join recipe r on do.recipe_id = r.id
        inner join glass_type gt on r.glass_type_id = gt.id
        where do.id = ?`

      row := db.QueryRow(sqlstr, orderdetails.OrderRef)
      var recipe_id string
      err := row.Scan(&orderdetails.Alcohol, &orderdetails.IdCheck, &orderdetails.DrinkName, &recipe_id, &orderdetails.Glass.Id, &orderdetails.Glass.Name)
      if err == sql.ErrNoRows {
        http.NotFound(w, r)
        return
      } else {
        if err != nil {
          panic(fmt.Sprintf("orderListHandler - failed to get order details: %#v", err))
        }
      }

      // Get list of ingrediants
      orderdetails.Ingredients = getRecipeIngrediants(db, recipe_id)
    }

    t, _ := template.ParseFiles("order_list.html")
    t.Execute(w, orderdetails)

}

// removeOrder is called when an order is selected and "remove" clicked. In reality it actaully cancels, not deletes, it.
func removeOrder(db *sql.DB, w http.ResponseWriter, r *http.Request, p string) bool {

  sqlstr := `
    update drink_order
    set cancelled = ?
    where id = ?
      and made_end_ts is null`

  _, err := db.Exec(sqlstr, true, p)

  if err != nil {
    panic(fmt.Sprintf("removeOrder failed: %#v", err))
  }
  return false
}


func makeOrder(db *sql.DB, w http.ResponseWriter, r *http.Request, p string) bool {
  var details OrderSent

  drink_order_id, err := strconv.Atoi(p)
  if err != nil {
    return false
  }

  details.OrderId = fmt.Sprintf(ORDER_FMT, drink_order_id)

  // Generate command list. This will fail if not all the ingrediants are present
  fmt.Printf("makeOrder: preparing command list for order [%d]\n", drink_order_id)
  cmdList, ret, recipe_id := getCommandList(drink_order_id, -1)

  details.Ingredients = getRecipeIngrediants(db, strconv.Itoa(recipe_id))

  if ret != 0 {
    fmt.Printf("makeOrder: failed to generate command list!\n")
    details.Success = false
    details.FailReason = "Missing ingrediant(s)"
    t, _ := template.ParseFiles("order_make.html")
    t.Execute(w, details)
    return true
  }
  details.Success = true

  // Record start time of order
  _, err = db.Exec(
    "update drink_order set made_start_ts = ? where id = ?",
    int32(time.Now().Unix()),
    drink_order_id,
  )
  if err != nil {
    panic(fmt.Sprintf("completeOrder: Failed to update db: %v", err))
  }

  BarbotSerialChan <- cmdList

  t, _ := template.ParseFiles("order_make.html")
  t.Execute(w, details)

  return true
}

// makeOrderDirect expect <p> to be the recipe_id
func makeOrderDirect(db *sql.DB, w http.ResponseWriter, r *http.Request, p string) bool {
  var details OrderSent

  recipe_id, err := strconv.Atoi(p)
  if err != nil {
    return false
  }

  tmpl, _ := template.ParseFiles("admin_header.html", "admin_make.html", "admin_footer.html")

  // Generate command list. This will fail if not all the ingrediants are present
  fmt.Printf("makeOrder: preparing command list for receipe [%d]\n", recipe_id)
  cmdList, ret, _ := getCommandList(-1, recipe_id)

  if ret != 0 {
    fmt.Printf("makeOrder: failed to generate command list!\n")
    details.Success = false
    details.FailReason = "Missing ingrediant(s)"
    t, _ := template.ParseFiles("order_make.html")
    t.Execute(w, details)
    return true
  }
  details.Success = true

  BarbotSerialChan <- cmdList

  var adminHead AdminHeader
  adminHead.AllowMaint = AllowMaint
  tmpl.ExecuteTemplate(w, "admin_header", adminHead)
  tmpl.ExecuteTemplate(w, "admin_make", details)
  tmpl.ExecuteTemplate(w, "admin_footer", nil)
  tmpl.Execute(w, details)

  return true
}
// completeOrder marks the drink as made in the database, then redirects to the order list
func completeOrder(db *sql.DB, w http.ResponseWriter, r *http.Request, p string) bool {

  drink_order_id, err := strconv.Atoi(p)
  if err != nil {
    return false
  }

  _, err = db.Exec(
    "update drink_order set made_end_ts = ? where id = ?",
    int32(time.Now().Unix()),
    drink_order_id,
  )
  if err != nil {
    panic(fmt.Sprintf("completeOrder: Failed to update db: %v", err))
  }

  http.Redirect(w, r, "/orderlist/", http.StatusSeeOther)

  return true
}

func recipeContainsAlcohol(tx *sql.Tx, recipe_id string) bool {

  sql := `
        SELECT     COUNT(1)
        FROM       recipe r
        INNER JOIN recipe_ingredient ri ON ri.recipe_id = r.id
        INNER JOIN ingredient i ON i.id = ri.ingredient_id
        WHERE      r.id = ?
        AND        alcoholic = 1`

  var alcoholic int
  row := tx.QueryRow(sql, recipe_id)

  err := row.Scan(&alcoholic)
  if err != nil {
    panic(fmt.Sprintf("recipeContainsAlcohol failed: %v", err))
    return true
  }

  if alcoholic > 0 {
    return true
  } else {
    return false
  }

}

func recipeIsVegan(tx *sql.Tx, recipe_id string) bool {

  sql := `SELECT NOT(SUM(NOT(i.vegan)) > 0)
          FROM   recipe r,
                 recipe_ingredient ri,
                 ingredient i
          WHERE  r.id = ?
          AND    ri.recipe_id = r.id
          AND    ri.ingredient_id = i.id`

  var vegan int
  row := tx.QueryRow(sql, recipe_id)

  err := row.Scan(&vegan)
  if err != nil {
    panic(fmt.Sprintf("recipeIsVegan failed: %v", err))
    return true
  }

  if vegan > 0 {
    return true
  } else {
    return false
  }

}
func orderDrinkHandler(w http.ResponseWriter, r *http.Request) {

    var err error
    var db *sql.DB

    if len(r.URL.Path) <= len("/order/") {
      http.NotFound(w, r)
      return
    } else {
       // Open database
      db = getDBConnection()
   }
   defer db.Close()
   tx, _ := db.Begin()
   defer tx.Rollback()

   recipe_id := r.URL.Path[len("/order/"):]

   // Check drink is known
   var menuitem MenuItem
   row := tx.QueryRow("select id, name from recipe where id = ?", recipe_id)
   err = row.Scan(&menuitem.Id, &menuitem.DrinkName)
   if err == sql.ErrNoRows {
     http.NotFound(w, r)
     return
   }

   alcoholic := recipeContainsAlcohol(tx, recipe_id)

   // Generate order
   _, insertErr := tx.Exec(
     "insert into drink_order (create_ts, recipe_id, alcohol, id_checked, cancelled) VALUES (?, ?, ?, ?, ?)",
     int32(time.Now().Unix()),
     recipe_id,
     alcoholic,
     false,
     false,
   )
   if insertErr != nil {
     panic(fmt.Sprintf("Insert order failed: %v", insertErr))
   }

   var orderLogged OrderLogged

    // Order reference (id)
    row = tx.QueryRow("select max(id) from drink_order")
    var order_id int
    err = row.Scan(&order_id)
    orderLogged.OrderId = fmt.Sprintf(ORDER_FMT, order_id)
    if err == sql.ErrNoRows {
      http.NotFound(w, r)
      return
    }
    tx.Commit()
    t, _ := template.ParseFiles("order_logged.html")
    t.Execute(w, orderLogged)
  }


// getDBConnection opens and returns a database connection
func getDBConnection() *sql.DB {
  // Open database
  db, err := sql.Open("sqlite3", "db.sqlite3")
  if err != nil {
    // TODO
    panic(fmt.Sprintf("%#v", err))
  }
  return db
}

// BBSerial goroutine manages serial communications with barbot
func BBSerial(instructionList chan []string, serialPort string) {

  // Open serial port
  port := &serial.Config{Name: serialPort, Baud: 115200}
  s, err := serial.OpenPort(port)
  if err != nil {
    panic(fmt.Sprintf("BBSerial failed to open serial port: %v", err))
  }


  serialReadChan := make(chan string)

  // read from serial port
  go func() {
    reader := bufio.NewReader(s)

    for {
      buf, err := reader.ReadBytes('\n')
      if err != nil {
        fmt.Printf("Error reading from serial port [%v]\n", err);
        return
      }
      var msg string
      msg = strings.Trim(fmt.Sprintf("%s", buf),"\n")
      if (len(buf) > 1) {
        serialReadChan <- msg
      }
    }
  }()

  for {
    select {
      case cmdList := <-instructionList:
        for _, cmd := range cmdList {
          fmt.Printf("> %s\n", cmd)
          _, err := s.Write([]byte(fmt.Sprintf("%s\n", cmd)))
          time.Sleep(10 * time.Millisecond) // 10ms delay between each instruction; don't send commands faster than the Arduino can process them
          if err != nil {
            panic(fmt.Sprintf("BBSerial: failed to transmit instruction: %v", err))
          }
        }

      case recieced_msg := <-serialReadChan:
        fmt.Printf("< %s\n", recieced_msg)
    }
  }

}

// getCommandList takes a drink_order_id, and returns a set of insturctions to be sent to
// barbot to make it, a retval (0=success) and the recipe_id
func getCommandList(drink_order_id int, recipe_id int) ([]string, int, int) {
/*
 * Instructions generated:
 *   M nnnnn               - move to rail position nnnnn
 *   D nn xxxx             - Dispense using dispenser nn, with parameter xxxx
 *
 */

   db := getDBConnection()
   defer db.Close()

   var sql_param int
   var ret_recipe_id int
   // Get a list of ingrediants required
   sqlstr := `select
                i.id,
                ri.qty,
                i.dispenser_param,
                dt.id,
               dt.unit_size,
               r.id
             `
  if drink_order_id > 0 { // order id given, use that...
    sqlstr += `
              from drink_order do
              inner join recipe r on r.id = do.recipe_id
              inner join recipe_ingredient ri on ri.recipe_id = r.id
              inner join ingredient i on i.id = ri.ingredient_id
              inner join dispenser_type dt on dt.id = i.dispenser_type_id
              where do.id = ?
                and dt.manual = 0
              order by ri.seq`
    sql_param = drink_order_id
   } else { // ... otherwise search by recipe_id
    sqlstr += `
              from recipe r
              inner join recipe_ingredient ri on ri.recipe_id = r.id
              inner join ingredient i on i.id = ri.ingredient_id
              inner join dispenser_type dt on dt.id = i.dispenser_type_id
              where r.id = ?
                and dt.manual = 0
              order by ri.seq`
    sql_param = recipe_id
  }

  rows, err := db.Query(sqlstr, sql_param)
  if err != nil {
    panic(fmt.Sprintf("%v", err))
  }
  defer rows.Close()


  commandList := make([]string, 1)

  // Clear any previous instructions
  commandList = append(commandList, fmt.Sprintf("C"))

  // Display order number
  commandList = append(commandList, fmt.Sprintf("O %d", drink_order_id))

  // Alway zero first
  commandList = append(commandList, fmt.Sprintf("Z"))
  for rows.Next() {
    var ingredient_id int
    var qty int
    var dispenser_param int
    var dispenser_type int
    var unit_size int

    rows.Scan(&ingredient_id, &qty, &dispenser_param, &dispenser_type, &unit_size, &ret_recipe_id)

    rail_position, dispenser_id := getIngredientPosition(ingredient_id)
    if dispenser_id == -1 {
      return nil, -1, ret_recipe_id
    }

    // move to the correct position
    commandList = append(commandList, fmt.Sprintf("M %d", rail_position))

    // Dispense
    switch dispenser_type {
      case DISPENSER_MIXER, DISPENSER_SYRINGE:
      // For the mixer and syringe, send qty as the number of milliseconds to dispense for
        adj_param := (qty * 1000) / dispenser_param

        // For the syringe, have a minimum despense time of 400ms (1 is a special case used to move the
        // syringe up - need a better way of doing this (TODO)
        if dispenser_type == DISPENSER_SYRINGE && adj_param < 400 {
          adj_param = 400
        }

        commandList = append(commandList, fmt.Sprintf("D% d %d", dispenser_id, adj_param))

      case DISPENSER_DASHER:
        // For dashers, the paramter is the number of dashes to despense
        commandList = append(commandList, fmt.Sprintf("D% d %d", dispenser_id, qty))

      default:
      for qty > 0 {
        qty--
        commandList = append(commandList, fmt.Sprintf("D% d %d", dispenser_id, dispenser_param))
      }
    }
  }

  // move to home position when done
  commandList = append(commandList, fmt.Sprintf("M 7080")) // TODO: move to home command.

  // Go!
  commandList = append(commandList, fmt.Sprintf("G"))

  return commandList, 0, ret_recipe_id
}

func getDrinkIcon(drinkName string, glassName string) string {
	re := regexp.MustCompile("[^a-zA-Z_ ]")
	imageFile := "static/images/receipes/" + strings.Replace(strings.ToLower(re.ReplaceAllString(drinkName, "")) + ".jpg", " ", "_", -1)
	if _, err := os.Stat(imageFile); os.IsNotExist(err) {
		fmt.Printf("could not find image file \"%s\"\n", imageFile)
		return "/static/images/receipes/0_generic_" + strings.ToLower(re.ReplaceAllString(glassName, "")) + ".jpg"
	} else {
		return "/" + imageFile
	}
}

// getIngredientPosition returns a suitable rail_position and dispenser_id for the requested ingrediant
func getIngredientPosition(ingredient_id int) (int, int) {

   db := getDBConnection()
   defer db.Close()

  sqlstr := `select d.id, d.rail_position
             from dispenser d
             inner join ingredient i on i.id = d.ingredient_id
             where i.id = ?
             `
  row := db.QueryRow(sqlstr, ingredient_id)

  var dispenser_id int
  var rail_position int

  err := row.Scan(&dispenser_id, &rail_position)
  if err == sql.ErrNoRows {
    fmt.Printf("getIngredientPosition: ingredient_id = %d not found!\n", ingredient_id)
    return -1, -1
  }
  if err != nil {
    panic(fmt.Sprintf("getIngredientPosition failed: %v", err))
  }
  fmt.Printf("getIngredientPosition: ingredient_id=[%d] is on dispenser_id=[%d], position=[%d]\n", ingredient_id, dispenser_id, rail_position)

  return rail_position,dispenser_id
}

func Secret(user, realm string) string {
  // Ignore all MD5 stuff - it's for the benifit of go-http-auth; the password is sent by
  // the browser in clear text and specifed on the command line in clear text.

  if user == "barbot" {

    e := auth.NewMD5Entry("$1$YeNsbWdH$wvOF8JdqsoiLix754LTW90") // used for salt
    if e == nil {
      return ""
    }
    result := auth.MD5Crypt([]byte(Password), e.Salt, e.Magic)

    return string(result)
  }
  return ""
}
func main() {
  // Two modes of operation:
  // 1. Normal - View drinks list at /menu/, order, then operator uses /orderlist/ to pick then make the drink
  // 2. Direct - Drinks menu available at /menu/, but no option to order. Drinks can be made by picking from
  //             a list at /admin/drinks/

  var serialPort = flag.String("serial", "/dev/ttyS0", "Serial port to use")
  var direct     = flag.Bool("direct", false, "Disable order interface")
  var password   = flag.String("password", "clubmate", "Password for order/admin interface");
  var allowMaint = flag.Bool("maint", false, "Allow entry into maintenance mode")
  flag.Parse()
  Direct = *direct
  Password = *password
  AllowMaint = *allowMaint;

  authenticator := auth.NewBasicAuthenticator("BarBot login", Secret)

  http.HandleFunc("/menu/", drinksMenuHandler)
  if !Direct {
  http.HandleFunc("/order/", orderDrinkHandler)
    http.HandleFunc("/orderlist/", auth.JustCheck(authenticator, orderListHandler))
  }

  http.HandleFunc("/admin/", auth.JustCheck(authenticator, adminHandler))
  http.Handle("/static/", http.StripPrefix("/static/", http.FileServer(http.Dir("static"))))
  http.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
       http.Redirect(w, r, "/menu/", http.StatusSeeOther)
       return
  })

  BarbotSerialChan = make(chan []string);
  go BBSerial(BarbotSerialChan, *serialPort)

  fmt.Printf("Started...\n")
  http.ListenAndServe(":8080", nil)
}

