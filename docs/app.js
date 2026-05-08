const milestoneData = {
  "0.1": {
    kicker: "Etap 0.1",
    title: "Pierwszy spojny shell",
    description: "Na tym etapie Karton zlozyl pierwszy czytelny zestaw: top panel, side dock, launcher, popupy i pierwsze polaczenie z uslugami sesji. To moment, w ktorym projekt przestal byc tylko zbiorem komponentow i zaczal wygladac jak jedno srodowisko.",
    status: "Aktualny fundament",
    scope: "Shell, popupy, sesja",
    caption: "Widok pierwszego spojnego desktopu z panelem, dockiem i popupami.",
    image: "gallery/0.1/0.1-panele.png",
    fallbackImage: "gallery/0.1/desktop-shell.svg",
    items: [
      "Pierwszy stabilny uklad top panelu i bocznego docka.",
      "Launcher, okna shellowe i popupy jako wspolny jezyk interfejsu.",
      "Wejscie w obsluge sesji: screenshot, powiadomienia i podstawowe narzedzia."
    ]
  },
  "0.2": {
    kicker: "Etap 0.2",
    title: "Integracja workflow i sesji",
    description: "Drugi etap dokreca relacje pomiedzy shellem a procesami sesyjnymi. Tu chodzi juz mniej o sam wyglad, a bardziej o przeplyw pracy: szybsze akcje, porzadniejsze stany, mniej rozjazdow miedzy panelem, launcherem i uslugami w tle.",
    status: "Nastepny krok",
    scope: "Workflow, spojnosc, restart",
    caption: "Kadr pokazujacy kierunek dopracowania codziennego workflow.",
    image: "gallery/0.2/0.2-workflow.png",
    fallbackImage: "gallery/0.2/workflow-pass.svg",
    items: [
      "Dopasowanie zachowania popupow do realnych akcji systemowych.",
      "Lepsza synchronizacja paneli z uslugami sesji i restartem komponentow.",
      "Wiecej uwagi na wygode codziennej pracy i czytelnosc interfejsu."
    ]
  },
  "0.3": {
    kicker: "Etap 0.3",
    title: "Pelniejszy system",
    description: "Trzeci etap to wyjscie poza sam shell i pokazanie bardziej kompletnego systemu. Nie chodzi tylko o kolejny ekran, ale o domkniecie tozsamosci Kartona jako lekkiego i spojnego srodowiska wokol Tektury.",
    status: "Kierunek rozwoju",
    scope: "System, spojnosc, produkt",
    caption: "Docelowy widok bardziej kompletnego systemu Karton.",
    image: "gallery/0.3/0.3-system.png",
    fallbackImage: "gallery/0.3/system-view.svg",
    items: [
      "Wieksza spojnosc miedzy kompozytorem, shellem i uslugami sesji.",
      "Lepsza prezentacja projektu jako gotowego produktu, a nie tylko eksperymentu.",
      "Uklad i narracja strony podporzadkowane historii rozwoju systemu."
    ]
  }
};

const buttons = Array.from(document.querySelectorAll(".timeline-stop"));
const shellPreview = document.getElementById("shell-preview");
const timelineSection = document.getElementById("timeline");
const milestoneKicker = document.getElementById("milestone-kicker");
const milestoneTitle = document.getElementById("milestone-title");
const milestoneDescription = document.getElementById("milestone-description");
const milestoneStatus = document.getElementById("milestone-status");
const milestoneScope = document.getElementById("milestone-scope");
const milestoneList = document.getElementById("milestone-list");
const milestoneImage = document.getElementById("milestone-image");
const milestoneCaption = document.getElementById("milestone-caption");

function setImageWithFallback(img, primarySrc, fallbackSrc, altText) {
  if (!img) {
    return;
  }

  img.alt = altText;

  if (!fallbackSrc || primarySrc === fallbackSrc) {
    img.src = primarySrc;
    return;
  }

  const probe = new Image();
  probe.onload = () => {
    img.src = primarySrc;
  };
  probe.onerror = () => {
    img.src = fallbackSrc;
  };
  probe.src = primarySrc;
}

function renderMilestone(version) {
  const milestone = milestoneData[version];
  if (!milestone) {
    return;
  }

  milestoneKicker.textContent = milestone.kicker;
  milestoneTitle.textContent = milestone.title;
  milestoneDescription.textContent = milestone.description;
  milestoneStatus.textContent = milestone.status;
  milestoneScope.textContent = milestone.scope;
  setImageWithFallback(
    milestoneImage,
    milestone.image,
    milestone.fallbackImage,
    `${milestone.title} screenshot`
  );
  milestoneCaption.textContent = milestone.caption;

  milestoneList.textContent = "";
  milestone.items.forEach((item) => {
    const li = document.createElement("li");
    li.textContent = item;
    milestoneList.appendChild(li);
  });

  buttons.forEach((button) => {
    const active = button.dataset.milestone === version;
    button.classList.toggle("is-active", active);
    button.setAttribute("aria-selected", active ? "true" : "false");
  });
}

buttons.forEach((button) => {
  button.addEventListener("click", () => {
    renderMilestone(button.dataset.milestone);
  });
});

shellPreview?.addEventListener("click", () => {
  timelineSection?.scrollIntoView({ behavior: "smooth", block: "start" });
});

renderMilestone("0.1");
